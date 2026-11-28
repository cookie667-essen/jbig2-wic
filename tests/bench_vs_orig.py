import re, subprocess, sys
from pathlib import Path
from PIL import Image

FOLDERS = [
    ("Test (7 Orig.)",  Path(r"C:\Users\Marius Koch\Desktop\Dither\Test")),
    ("01_scans (40)",   Path(r"C:\Users\Marius Koch\Desktop\Dither\Corpus100\01_scans")),
    ("02_text_mixed",   Path(r"C:\Users\Marius Koch\Desktop\Dither\Corpus100\02_text_mixed")),
    ("03_graphics",     Path(r"C:\Users\Marius Koch\Desktop\Dither\Corpus100\03_graphics")),
    ("04_photos (15)",  Path(r"C:\Users\Marius Koch\Desktop\Dither\Corpus100\04_photos")),
    ("05_edgecases",    Path(r"C:\Users\Marius Koch\Desktop\Dither\Corpus100\05_edgecases")),
]

RE_MP = re.compile(
    r"MULTI-PAGE \(Global-Dict\)\s*\|\s*([\d.]+)\s*KB\s*\|\s*([\d.]+)ms\s*\|\s*([\d.]+)\s*KB\s*\|\s*([\d.]+)ms"
)

def pct(val: float, ref: float) -> str:
    return f"{(val - ref) / ref * 100.0:+6.1f}%" if ref > 0 else "   n/a "

hdr = (
    f"{'Kategorie':<16} | {'Raw 1bpp':>9} | {'Orig PNG':>9} | "
    f"{'JBIG2 Gen':>9} | {'vs PNG':>7} | {'JBIG2 Sym':>9} | {'vs PNG':>7} | {'Sym/Gen':>7} | {'Gen/Sym ms':>12}"
)
print(hdr)
print("-" * len(hdr))

tot_raw = tot_png = tot_gen = tot_sym = 0.0

for label, folder in FOLDERS:
    if not folder.exists():
        continue
    pngs = sorted(folder.glob("*.png"))
    png_kb = sum(p.stat().st_size for p in pngs) / 1024.0
    raw_kb = 0.0
    for p in pngs:
        with Image.open(p) as im:
            w, h = im.size
            raw_kb += (((w + 7) // 8) * h) / 1024.0

    proc = subprocess.run(
        [sys.executable, "tests/bench_symbol.py", str(folder)],
        capture_output=True, text=True, check=True
    )
    m = RE_MP.search(proc.stdout)
    if not m:
        print(f"{label:<16} | Konnte MULTI-PAGE-Zeile nicht parsen")
        continue

    gen_kb, gen_ms, sym_kb, sym_ms = map(float, m.groups())
    tot_raw += raw_kb; tot_png += png_kb; tot_gen += gen_kb; tot_sym += sym_kb

    print(
        f"{label:<16} | {raw_kb:6.1f} KB | {png_kb:6.1f} KB | "
        f"{gen_kb:6.1f} KB | {pct(gen_kb, png_kb)} | {sym_kb:6.1f} KB | {pct(sym_kb, png_kb)} | "
        f"{pct(sym_kb, gen_kb)} | {gen_ms:4.1f}/{sym_ms:5.1f}ms"
    )

print("-" * len(hdr))
print(
    f"{'GESAMT (107)':<16} | {tot_raw:6.1f} KB | {tot_png:6.1f} KB | "
    f"{tot_gen:6.1f} KB | {pct(tot_gen, tot_png)} | {tot_sym:6.1f} KB | {pct(tot_sym, tot_png)} | "
    f"{pct(tot_sym, tot_gen)} |"
)