import ctypes, subprocess, sys, tempfile, time
from pathlib import Path
from PIL import Image

EXTS = {".png", ".tif", ".tiff", ".bmp", ".jpg", ".jpeg", ".pbm", ".webp"}
DLL = Path("build/Release/jbig2_wic.dll").resolve()
TEST_EXE = Path("build/Release/encoder_test.exe").resolve()
lib = ctypes.WinDLL(str(DLL))

class Page(ctypes.Structure):
    _fields_ = [("pixels", ctypes.c_void_p), ("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32), ("stride", ctypes.c_uint32),
                ("dpi_x", ctypes.c_double), ("dpi_y", ctypes.c_double)]

lib.jbig2_encode_multipage_1bpp.argtypes = [
    ctypes.POINTER(Page), ctypes.c_uint32, ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t)
]
lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

def encode_pages(pages_arr, count, flags):
    ptr, sz = ctypes.POINTER(ctypes.c_uint8)(), ctypes.c_size_t(0)
    t0 = time.perf_counter()
    rc = lib.jbig2_encode_multipage_1bpp(pages_arr, count, flags, ctypes.byref(ptr), ctypes.byref(sz))
    dt = (time.perf_counter() - t0) * 1000.0
    assert rc == 0, f"Encode rc={rc}"
    data = ctypes.string_at(ptr, sz.value)
    lib.jbig2_free_buffer(ptr)
    return data, dt

def verify_roundtrip(data: bytes) -> str:
    with tempfile.TemporaryDirectory() as td:
        p_in, p_out = Path(td) / "in.jb2", Path(td) / "out.jb2"
        p_in.write_bytes(data)
        r = subprocess.run([str(TEST_EXE), str(p_in), str(p_out)], capture_output=True, text=True)
        return "0 Bit" if (r.returncode == 0 and "0 Bit" in r.stdout) else f"ERR({r.returncode})"

folder = Path(sys.argv[1])
files = sorted(p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in EXTS)
print(f"Gefundene Bilddateien in {folder}: {len(files)}\n")
print(f"{'Datei':<30} | {'Generic':>9} | {'Gen ms':>7} | {'Symbol':>9} | {'Sym ms':>7} | {'Delta':>7} | Check")
print("-" * 88)

raw_bufs, page_structs = [], []
tot_g = tot_s = tot_gt = tot_st = 0.0

for p in files:
    img = Image.open(p).convert("1")
    w, h = img.size
    stride = (w + 7) // 8
    raw = ctypes.create_string_buffer(img.tobytes(), stride * h)
    pg = Page(ctypes.addressof(raw), w, h, stride, 300.0, 300.0)
    raw_bufs.append(raw)
    page_structs.append(pg)

    gb, gt = encode_pages(ctypes.byref(pg), 1, 0)
    sb, st = encode_pages(ctypes.byref(pg), 1, 2)
    chk = verify_roundtrip(sb)
    delta = (len(sb) - len(gb)) / len(gb) * 100.0
    tot_g += len(gb); tot_s += len(sb); tot_gt += gt; tot_st += st
    print(f"{p.name[:30]:<30} | {len(gb)/1024:6.1f} KB | {gt:5.1f}ms | {len(sb)/1024:6.1f} KB | {st:5.1f}ms | {delta:+6.1f}% | {chk}")

print("-" * 88)
tdelta = (tot_s - tot_g) / tot_g * 100.0 if tot_g else 0.0
print(f"{'GESAMT (Einzelseiten)':<30} | {tot_g/1024:6.1f} KB | {tot_gt:5.1f}ms | {tot_s/1024:6.1f} KB | {tot_st:5.1f}ms | {tdelta:+6.1f}% |")

if len(page_structs) > 1:
    arr = (Page * len(page_structs))(*page_structs)
    mgb, mgt = encode_pages(arr, len(page_structs), 0)
    msb, mst = encode_pages(arr, len(page_structs), 2)
    mdelta = (len(msb) - len(mgb)) / len(mgb) * 100.0
    print(f"{'MULTI-PAGE (Global-Dict)':<30} | {len(mgb)/1024:6.1f} KB | {mgt:5.1f}ms | {len(msb)/1024:6.1f} KB | {mst:5.1f}ms | {mdelta:+6.1f}% |")
