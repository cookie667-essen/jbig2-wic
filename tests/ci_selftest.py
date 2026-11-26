import ctypes, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DLL = ROOT / "build" / "Release" / "jbig2_wic.dll"
TEST_EXE = ROOT / "build" / "Release" / "encoder_test.exe"
CORE_EXE = ROOT / "build" / "Release" / "core_test.exe"

class Page(ctypes.Structure):
    _fields_ = [
        ("pixels", ctypes.c_void_p), ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32), ("stride", ctypes.c_uint32),
        ("dpi_x", ctypes.c_double), ("dpi_y", ctypes.c_double),
    ]

lib = ctypes.WinDLL(str(DLL))
lib.jbig2_encode_multipage_1bpp.argtypes = [
    ctypes.POINTER(Page), ctypes.c_uint32, ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t),
]
lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

def set_black(buf: bytearray, stride: int, x: int, y: int) -> None:
    buf[y * stride + (x >> 3)] &= ~(1 << (7 - (x & 7)))

def draw_glyph_f(buf: bytearray, stride: int, ox: int, oy: int, variant: int) -> None:
    for y in range(13):
        set_black(buf, stride, ox + 0, oy + y)
        set_black(buf, stride, ox + 1, oy + y)
    for x in range(11):
        set_black(buf, stride, ox + x, oy + 0)
        set_black(buf, stride, ox + x, oy + 1)
    for x in range(9):
        set_black(buf, stride, ox + x, oy + 6)
        set_black(buf, stride, ox + x, oy + 7)
    if variant == 0:
        set_black(buf, stride, ox + 11, oy + 1)
        set_black(buf, stride, ox + 0, oy + 13)
    elif variant == 1:
        set_black(buf, stride, ox + 0, oy + 13)
    elif variant == 2:
        set_black(buf, stride, ox + 11, oy + 1)
    elif variant == 4:
        set_black(buf, stride, ox + 11, oy + 1)
        set_black(buf, stride, ox + 12, oy + 1)
        set_black(buf, stride, ox + 0, oy + 13)
        set_black(buf, stride, ox + 0, oy + 14)

def draw_glyph_h(buf: bytearray, stride: int, ox: int, oy: int, noise: bool) -> None:
    for y in range(14):
        for dx in (0, 1, 9, 10):
            set_black(buf, stride, ox + dx, oy + y)
    for x in range(11):
        set_black(buf, stride, ox + x, oy + 6)
        set_black(buf, stride, ox + x, oy + 7)
    if noise:
        set_black(buf, stride, ox + 2, oy + 5)
        set_black(buf, stride, ox + 8, oy + 8)

def make_test_page(w: int, h: int, seed_shift: int) -> tuple[ctypes.Array, Page]:
    stride = (w + 7) // 8
    buf = bytearray([0xFF] * (stride * h))
    for row in range(6):
        y = 24 + row * 40
        for col in range(12):
            x = 24 + col * 28
            if (row + col + seed_shift) % 2 == 0:
                draw_glyph_f(buf, stride, x, y, (row * 12 + col) % 5)
            else:
                draw_glyph_h(buf, stride, x, y, ((row + col) % 3) == 1)
    raw = ctypes.create_string_buffer(bytes(buf), len(buf))
    return raw, Page(ctypes.addressof(raw), w, h, stride, 300.0, 300.0)

def encode(pages: list[Page], flags: int) -> bytes:
    arr = (Page * len(pages))(*pages)
    ptr, sz = ctypes.POINTER(ctypes.c_uint8)(), ctypes.c_size_t(0)
    rc = lib.jbig2_encode_multipage_1bpp(arr, len(pages), flags, ctypes.byref(ptr), ctypes.byref(sz))
    assert rc == 0, f"jbig2_encode_multipage_1bpp fehlgeschlagen: rc={rc}"
    data = ctypes.string_at(ptr, sz.value)
    lib.jbig2_free_buffer(ptr)
    return data

def parse_segments(data: bytes) -> list[tuple[int, int, int]]:
    pos = 13
    segs = []
    while pos + 11 <= len(data):
        seg_num = struct.unpack_from(">I", data, pos)[0]; pos += 4
        flags = data[pos]; pos += 1
        seg_type = flags & 0x3F
        ref_cnt = data[pos] >> 5; pos += 1
        ref_sz = 1 if seg_num <= 256 else (2 if seg_num <= 65536 else 4)
        pos += ref_cnt * ref_sz
        page = struct.unpack_from(">I", data, pos)[0] if (flags & 0x40) else data[pos]
        pos += 4 if (flags & 0x40) else 1
        dlen = struct.unpack_from(">I", data, pos)[0]; pos += 4
        pos += dlen
        segs.append((seg_type, page, dlen))
    return segs

def compare_check(ref_data: bytes, test_data: bytes, label: str) -> None:
    with tempfile.TemporaryDirectory() as td:
        p_ref, p_tst = Path(td) / "ref.jb2", Path(td) / "tst.jb2"
        p_ref.write_bytes(ref_data)
        p_tst.write_bytes(test_data)
        r = subprocess.run([str(TEST_EXE), "--compare", str(p_ref), str(p_tst)], capture_output=True, text=True)
        assert r.returncode == 0, f"{label}: encoder_test rc={r.returncode}\n{r.stdout}\n{r.stderr}"
        assert "0 Bit" in r.stdout, f"{label}: Bit-Abweichung!\n{r.stdout}"

def main() -> int:
    raw0, pg0 = make_test_page(400, 300, 0)
    gen1 = encode([pg0], 0)
    sym1 = encode([pg0], 2)
    compare_check(gen1, gen1, "Single-Page Generic")
    compare_check(gen1, sym1, "Single-Page Symbol vs Generic")
    assert len(sym1) < len(gen1), f"Symbol ({len(sym1)} B) nicht kleiner als Generic ({len(gen1)} B)"
    print(f"[OK] Single-Page: Generic={len(gen1)} B | Symbol={len(sym1)} B ({(len(sym1)-len(gen1))/len(gen1)*100:+.1f}%) | 0 Bit vs Generic")

    raws, pgs = [], []
    for i in range(3):
        r, p = make_test_page(400, 300, i)
        raws.append(r); pgs.append(p)
    mgen = encode(pgs, 0)
    msym = encode(pgs, 2)
    segs = parse_segments(msym)
    assert any(t == 0 and p == 0 for t, p, _ in segs), f"Kein globales Segment 0 (Page=0): {segs}"
    text_pages = {p for t, p, _ in segs if t == 7}
    assert text_pages == {1, 2, 3}, f"Erwartete Text-Regionen auf Seiten 1..3, erhalten: {text_pages}"
    assert len(msym) < len(mgen), f"Multi-Page Symbol ({len(msym)} B) nicht kleiner als Generic ({len(mgen)} B)"
    compare_check(mgen, msym, "Multi-Page Global-Dict vs Generic")
    print(f"[OK] Multi-Page (3 Seiten): Generic={len(mgen)} B | Symbol={len(msym)} B ({(len(msym)-len(mgen))/len(mgen)*100:+.1f}%) | Global-Dict OK | 0 Bit (alle 3 Seiten)")

    ls1 = encode([pg0], 6)
    assert len(ls1) < len(sym1), f'LossySafe ({len(ls1)} B) nicht kleiner als Lossless ({len(sym1)} B)'
    with tempfile.TemporaryDirectory() as td:
        pj, pr = Path(td) / 'ls1.jb2', Path(td) / 'ls1.raw'
        pj.write_bytes(ls1)
        r = subprocess.run([str(CORE_EXE), str(pj), str(pr)], capture_output=True, text=True)
        assert r.returncode == 0, f'LossySafe Single-Page core_test rc={r.returncode}'
        aa, bb = bytes(raw0), pr.read_bytes()
        w, h, st = pg0.width, pg0.height, pg0.stride
        rm = ((1 << w) - 1) << (st * 8 - w)
        xs = [(int.from_bytes(aa[y*st:(y+1)*st], 'big') ^ int.from_bytes(bb[y*st:(y+1)*st], 'big')) & rm for y in range(h)]
        h2 = [x & (x >> 1) for x in xs]
        dpx = sum(x.bit_count() for x in xs)
        b2 = sum((h2[y] & h2[y-1]).bit_count() for y in range(1, h))
        assert b2 == 0, f'LossySafe 2x2-Fehlerbloecke gefunden: {b2}'
    print(f'[OK] Single-Page LossySafe: {len(ls1)} B ({(len(ls1)-len(gen1))/len(gen1)*100:+.1f}% vs Gen, {(len(ls1)-len(sym1))/len(sym1)*100:+.1f}% vs LL) | Diff={dpx} px | 2x2-Blk=0')

    mls = encode(pgs, 6)
    lsegs = parse_segments(mls)
    assert any(t == 0 and p == 0 for t, p, _ in lsegs), f'LossySafe: Kein globales Segment 0: {lsegs}'
    assert {p for t, p, _ in lsegs if t == 7} == {1, 2, 3}, 'LossySafe: Text-Regionen fehlen'
    assert len(mls) < len(msym), f'Multi-Page LossySafe ({len(mls)} B) nicht kleiner als Lossless ({len(msym)} B)'
    with tempfile.TemporaryDirectory() as td:
        pj = Path(td) / 'mls.jb2'
        pj.write_bytes(mls)
        r = subprocess.run([str(CORE_EXE), str(pj)], capture_output=True, text=True)
        assert r.returncode == 0, f'LossySafe Multi-Page core_test rc={r.returncode}'
    print(f'[OK] Multi-Page LossySafe (3 Seiten): {len(mls)} B ({(len(mls)-len(mgen))/len(mgen)*100:+.1f}% vs Gen, {(len(mls)-len(msym))/len(msym)*100:+.1f}% vs LL) | Global-Dict OK')
    return 0

if __name__ == "__main__":
    sys.exit(main())
