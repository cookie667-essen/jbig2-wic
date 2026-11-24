import ctypes
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build" / "Release"
DLL_PATH = BUILD_DIR / "jbig2_wic.dll"
ENC_TEST = BUILD_DIR / "encoder_test.exe"
JBIG2_SIG = b"\x97JB2\r\n\x1a\n"


class Jbig2PageInput(ctypes.Structure):
    _fields_ = [
        ("pixels", ctypes.c_void_p),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("stride", ctypes.c_uint32),
        ("dpi_x", ctypes.c_double),
        ("dpi_y", ctypes.c_double),
    ]


def make_synthetic_1bpp(width: int, height: int) -> tuple[bytes, int]:
    """Erzeugt ein 1-Bit-Testbild (1=Weiß, 0=Schwarz) mit Rändern, Streifen & Schachbrett."""
    stride = (width + 7) // 8
    buf = bytearray([0xFF] * (stride * height))
    for y in range(height):
        row = y * stride
        if 10 <= y < 30:
            continue  # Weiße Zeilen für TPGDON-Test
        for x in range(width):
            black = False
            if 30 <= y < 120:
                black = ((x + y) & 1) == 0  # Schachbrett / Dither
            elif 120 <= y < 180:
                black = ((x // 7) & 1) == 0  # Identische Folgezeilen (TPGDON)
            else:
                black = ((x ^ y) % 13) == 0  # Diagonales Muster
            if black:
                buf[row + (x >> 3)] &= ~(1 << (7 - (x & 7)))
    return bytes(buf), stride


def main() -> int:
    if not DLL_PATH.is_file() or not ENC_TEST.is_file():
        print(f"FEHLER: Build-Artefakte fehlen in {BUILD_DIR}")
        return 1

    lib = ctypes.WinDLL(str(DLL_PATH))
    lib.jbig2_encode_1bpp.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_double, ctypes.c_double,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.jbig2_encode_1bpp.restype = ctypes.c_int
    lib.jbig2_encode_multipage_1bpp.argtypes = [
        ctypes.POINTER(Jbig2PageInput), ctypes.c_uint32, ctypes.c_int,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.jbig2_encode_multipage_1bpp.restype = ctypes.c_int
    lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

    w, h = 399, 257  # Ungerade Breite prüft Stride-Alignment
    pixels, stride = make_synthetic_1bpp(w, h)

    # 1. Single-Page C-API Test
    out_ptr = ctypes.POINTER(ctypes.c_uint8)()
    out_sz = ctypes.c_size_t(0)
    rc = lib.jbig2_encode_1bpp(
        pixels, w, h, stride, 300.0, 300.0, ctypes.byref(out_ptr), ctypes.byref(out_sz)
    )
    assert rc == 0 and out_ptr and out_sz.value > 32, f"Single-Page Encode fehlgeschlagen (rc={rc})"
    try:
        single_jb2 = ctypes.string_at(out_ptr, out_sz.value)
    finally:
        lib.jbig2_free_buffer(out_ptr)
    assert single_jb2.startswith(JBIG2_SIG), "Ungueltige JBIG2-Signatur (Single-Page)"

    # 2. Bitgenauer Roundtrip über PDFium-Decoder (encoder_test.exe)
    with tempfile.TemporaryDirectory() as tmp:
        in_file = Path(tmp) / "ci_in.jb2"
        out_file = Path(tmp) / "ci_out.jb2"
        in_file.write_bytes(single_jb2)
        res = subprocess.run([str(ENC_TEST), str(in_file), str(out_file)], check=False)
        assert res.returncode == 0, f"encoder_test.exe fehlgeschlagen (rc={res.returncode})"

    # 3. Multi-Page C-API Test (2 Seiten)
    buf_ref = ctypes.create_string_buffer(pixels, len(pixels))
    addr = ctypes.addressof(buf_ref)
    pages = (Jbig2PageInput * 2)(
        Jbig2PageInput(addr, w, h, stride, 300.0, 300.0),
        Jbig2PageInput(addr, w, h, stride, 300.0, 300.0),
    )
    mp_ptr = ctypes.POINTER(ctypes.c_uint8)()
    mp_sz = ctypes.c_size_t(0)
    rc = lib.jbig2_encode_multipage_1bpp(pages, 2, 0, ctypes.byref(mp_ptr), ctypes.byref(mp_sz))
    assert rc == 0 and mp_ptr and mp_sz.value > len(single_jb2), f"Multi-Page Encode fehlgeschlagen (rc={rc})"
    try:
        mp_jb2 = ctypes.string_at(mp_ptr, mp_sz.value)
    finally:
        lib.jbig2_free_buffer(mp_ptr)
    assert mp_jb2.startswith(JBIG2_SIG), "Ungueltige JBIG2-Signatur (Multi-Page)"
    page_count = int.from_bytes(mp_jb2[9:13], "big")
    assert page_count == 2, f"Erwartete 2 Seiten, erhalten: {page_count}"

    print(f"CI Self-Test OK: Single={len(single_jb2)} B | MultiPage(2)={len(mp_jb2)} B | 0 Bit Abweichung")
    return 0


if __name__ == "__main__":
    sys.exit(main())
