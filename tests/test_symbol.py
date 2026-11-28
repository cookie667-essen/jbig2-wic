import ctypes
import subprocess
import tempfile
from pathlib import Path

lib = ctypes.WinDLL(str(Path("build/Release/jbig2_wic.dll").resolve()))

class Jbig2PageInput(ctypes.Structure):
    _fields_ = [
        ("pixels", ctypes.c_void_p),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("stride", ctypes.c_uint32),
        ("dpi_x", ctypes.c_double),
        ("dpi_y", ctypes.c_double),
    ]

lib.jbig2_encode_multipage_1bpp.argtypes = [
    ctypes.POINTER(Jbig2PageInput), ctypes.c_uint32, ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t),
]
lib.jbig2_encode_multipage_1bpp.restype = ctypes.c_int
lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

# Synthetische Seite: 135 identische "H"-Glyphen + 135 "O"-Glyphen
w, h = 400, 300
stride = (w + 7) // 8
buf = bytearray([0xFF] * (stride * h))
glyphs = [
    ["11000011", "11000011", "11000011", "11111111", "11000011", "11000011", "11000011", "11000011", "11000011"],
    ["01111110", "11000011", "11000011", "11000011", "11000011", "11000011", "11000011", "11000011", "01111110"],
]
for idx, ry in enumerate(range(16, 260, 20)):
    for jdx, rx in enumerate(range(16, 370, 16)):
        g = glyphs[(idx + jdx) & 1]
        for dy, row in enumerate(g):
            for dx, ch in enumerate(row):
                if ch == "1":
                    buf[(ry + dy) * stride + ((rx + dx) >> 3)] &= ~(1 << (7 - ((rx + dx) & 7)))

raw = ctypes.create_string_buffer(bytes(buf), len(buf))
pg = Jbig2PageInput(ctypes.addressof(raw), w, h, stride, 300.0, 300.0)

def encode(flags: int) -> bytes:
    ptr = ctypes.POINTER(ctypes.c_uint8)()
    sz = ctypes.c_size_t(0)
    rc = lib.jbig2_encode_multipage_1bpp(ctypes.byref(pg), 1, flags, ctypes.byref(ptr), ctypes.byref(sz))
    assert rc == 0 and ptr and sz.value > 0, f"Encode fehlgeschlagen (rc={rc})"
    try:
        return ctypes.string_at(ptr, sz.value)
    finally:
        lib.jbig2_free_buffer(ptr)

b_gen = encode(0)
b_sym = encode(2)

with tempfile.TemporaryDirectory() as tmp:
    in_f = Path(tmp) / "sym.jb2"
    out_f = Path(tmp) / "re.jb2"
    in_f.write_bytes(b_sym)
    rc = subprocess.run(["build/Release/encoder_test.exe", str(in_f), str(out_f)]).returncode

print(f"Generic = {len(b_gen)} B | Symbol = {len(b_sym)} B | Ersparnis = {100*(1 - len(b_sym)/len(b_gen)):.1f}% | PDFium-Roundtrip rc = {rc}")
