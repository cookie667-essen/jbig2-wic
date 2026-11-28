import ctypes, subprocess, tempfile, time
from pathlib import Path
from PIL import Image

lib = ctypes.WinDLL(str(Path("build/Release/jbig2_wic.dll").resolve()))

class Page(ctypes.Structure):
    _fields_ = [("p", ctypes.c_void_p), ("w", ctypes.c_uint32), ("h", ctypes.c_uint32),
                ("s", ctypes.c_uint32), ("dx", ctypes.c_double), ("dy", ctypes.c_double)]

fn = lib.jbig2_encode_multipage_1bpp
fn.argtypes = [ctypes.POINTER(Page), ctypes.c_uint32, ctypes.c_int,
               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_size_t)]
lib.jbig2_free_buffer.argtypes = [ctypes.c_void_p]

def enc(pg: Page, flags: int) -> tuple[bytes, float]:
    ptr, sz = ctypes.POINTER(ctypes.c_uint8)(), ctypes.c_size_t(0)
    t0 = time.perf_counter()
    assert fn(ctypes.byref(pg), 1, flags, ctypes.byref(ptr), ctypes.byref(sz)) == 0
    dt = (time.perf_counter() - t0) * 1000.0
    data = ctypes.string_at(ptr, sz.value)
    lib.jbig2_free_buffer(ptr)
    return data, dt

def make_cell_halftone(w: int = 512, h: int = 512) -> Image.Image:
    # Klassisches 4x4-AM-Zellenraster (17 Helligkeitsstufen)
    bayer = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
    im = Image.new("1", (w, h), 1)
    px = im.load()
    for gy in range(0, h, 4):
        for gx in range(0, w, 4):
            lvl = int(((gx + gy) / (w + h)) * 16)
            for dy in range(4):
                for dx in range(4):
                    px[gx + dx, gy + dy] = 0 if bayer[dy * 4 + dx] < lvl else 1
    return im

files = sorted(Path(r"C:\Users\Marius Koch\Desktop\Dither\corpus100").rglob("*bayer4*.png"))
items = [(f.name, Image.open(f).convert("1", dither=Image.Dither.NONE)) for f in files]
items.append(("synth_cell_4x4.png", make_cell_halftone()))

print(f"{'Datei':22s} | {'Generic':>9s} | {'Halftone':>9s} | {'vs Gen':>7s} | {'Zeit':>7s} | Compare")
print("-" * 80)
with tempfile.TemporaryDirectory() as td:
    pg_f, ph_f = Path(td) / "g.jb2", Path(td) / "h.jb2"
    for name, im in items:
        raw = im.tobytes()
        buf = ctypes.create_string_buffer(raw, len(raw))
        pg = Page(ctypes.cast(buf, ctypes.c_void_p), im.width, im.height, (im.width + 7) // 8, 300.0, 300.0)
        gb, _ = enc(pg, 0)
        hb, dt = enc(pg, 8)
        pg_f.write_bytes(gb)
        ph_f.write_bytes(hb)
        r = subprocess.run(["build/Release/encoder_test.exe", "--compare", str(pg_f), str(ph_f)],
                           capture_output=True, text=True)
        ok = "0 Bit OK" if (r.returncode == 0 and "0 Bit" in r.stdout) else f"FAIL rc={r.returncode}"
        print(f"{name:22s} | {len(gb):8d}B | {len(hb):8d}B | {(len(hb)-len(gb))/len(gb)*100:+6.1f}% | {dt:5.1f}ms | {ok}")
