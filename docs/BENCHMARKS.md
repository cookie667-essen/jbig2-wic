# Benchmarks

All measurements were taken with **MSVC Release (LTCG)** builds and verified for **0-bit difference** against the original 1-bit input bitmaps (`--compare` roundtrip).

---

## 1. Optimization Progression (7-Page Real-World Reference Set)

Mixed document set containing 3 text-heavy pages (`Vorlage3_fs`, `EON`, `Vorlage1_fs`) and 4 heavily dithered scans (`Brief.7_fs`, `Labor_fs`, `Stellungnahme`, `Testpage_fs`).

| Metric | Baseline (pre-v2.2) | v2.2.0 | **v2.3.0** | Speedup vs. Baseline |
|---|---:|---:|---:|---:|
| **Multi-Page Generic** (669.5 KB) | 132.3 ms | 44.0 ms | **38.1 ms** | **3.5x** |
| **Multi-Page Symbol** (646.6 KB, -3.4%) | 400.3 ms | 113.7 ms | **97.6-103.5 ms** | **~4.0x** |
| **Single-Page Symbol Sum** (657.5 KB) | 400.3 ms | 263.6 ms | **172.2 ms** *(1.29x Gen)* | **2.3x** |
| **Single-Page `Brief.7_fs` (dither scan)** | 106.6 ms | 62.5 ms | **36.1 ms** *(Gen: 34.1 ms)* | **3.0x** |

### Per-Page Breakdown (v2.3.0)

| File | Generic | Gen ms | Symbol | Sym ms | Sym vs. Gen | Bit Diff |
|---|---:|---:|---:|---:|---:|---|
| `Brief.7_fs.png` | 193.0 KB | 34.1 ms | 193.0 KB | 36.1 ms | +0.0% | 0 Bit |
| `EON.png` | 25.9 KB | 8.5 ms | 23.6 KB | 14.9 ms | **-8.8%** | 0 Bit |
| `Labor_fs.png` | 182.2 KB | 31.8 ms | 182.2 KB | 35.2 ms | +0.0% | 0 Bit |
| `Stellungnahme_fs.png` | 118.9 KB | 22.7 ms | 118.9 KB | 24.1 ms | +0.0% | 0 Bit |
| `Testpage_fs.png` | 63.4 KB | 14.7 ms | 63.4 KB | 16.4 ms | +0.0% | 0 Bit |
| `Vorlage1_fs.png` | 56.1 KB | 12.9 ms | 53.8 KB | 28.9 ms | **-4.1%** | 0 Bit |
| `Vorlage3_fs.png` | 30.2 KB | 8.7 ms | 22.6 KB | 16.6 ms | **-25.1%** | 0 Bit |
| **Single-Page Total** | **669.7 KB** | **133.3 ms** | **657.5 KB** | **172.2 ms** | **-1.8%** | **0 Bit** |
| **Multi-Page (Global Dict)** | **669.5 KB** | **38.1 ms** | **646.6 KB** | **103.5 ms** | **-3.4%** | **0 Bit** |

---

## 2. 107-File Multi-Category Corpus (v2.3.0, Multi-Page per Category)

Tested across 107 1-bit PNG files: 7 real-world reference documents, 40 FUNSD scans (30 Otsu + 10 Floyd-Steinberg), 20 synthetic text/mixed pages, 15 line graphics, 15 dithered Kodak photos (10 FS + 5 Bayer 4x4), and 10 synthetic stress edge cases (`1x1`, `7x17`, `4097x9`, checkerboard, 50% random noise, 10k speckles, frame+text, exact `64x72` glyphs).

| Category | Raw 1bpp | Orig PNG | JBIG2 Gen | Gen vs. PNG | JBIG2 Sym | Sym vs. PNG | Sym vs. Gen | Gen / Sym Time |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **Test (7 Real-World)** | 7,434.2 KB | 1,390.7 KB | 669.5 KB | -51.9% | **646.6 KB** | **-53.5%** | -3.4% | 37.8 / 97.6 ms |
| **01_scans (40 FUNSD)** | 3,743.2 KB | 418.9 KB | 276.2 KB | -34.1% | **276.2 KB** | **-34.1%** | +0.0% | 11.4 / 54.7 ms |
| **02_text_mixed (20)**\* | 9,456.5 KB | 245.9 KB | 343.4 KB | +39.7% | **165.6 KB** | **-32.7%** | **-51.8%** | 16.8 / 58.9 ms |
| **03_graphics (15)** | 3,515.6 KB | 321.5 KB | 77.5 KB | -75.9% | **77.5 KB** | **-75.9%** | +0.0% | 8.9 / 11.2 ms |
| **04_photos (15 Kodak)** | 2,880.0 KB | 1,964.1 KB | 943.5 KB | -52.0% | **943.5 KB** | **-52.0%** | +0.0% | 32.7 / 63.3 ms |
| **05_edgecases (10)** | 1,195.2 KB | 154.4 KB | 163.7 KB | +6.0% | **153.0 KB** | **-0.9%** | -6.5% | 18.4 / 19.3 ms |
| **TOTAL (107 Files)** | **28,224.8 KB** | **4,495.4 KB** | **2,473.8 KB** | **-45.0%** | **2,262.4 KB** | **-49.7%** | **-8.5%** | **0 Bit Diff** |

\* *Note on `02_text_mixed`:* The synthetic pages repeat identical paragraph strings 28 times per page, allowing PNG's 32 KB LZ77 sliding window to back-reference entire paragraphs. Even in this worst-case comparison for 2D generic coding, JBIG2 Symbol Coding still beats PNG by **-32.7%** (and beats JBIG2 Generic by **-51.8%**).
