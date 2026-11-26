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

---

## 3. Reference Comparison: `jbig2-wic v2.3.0` vs. `jbig2.exe` (`jbig2enc`)

To ensure a fair speed comparison without `libpng` decoding overhead, `jbig2.exe` was fed uncompressed binary **P4 PBM** files warmed in the OS page cache. Because `jbig2.exe` only encodes one page per invocation in lossless generic mode, the median process-spawn baseline (`jbig2.exe -V` = **6.8 ms/call**) is subtracted to report net encoding time alongside wall-clock time.

| Category | `jbig2.exe` Gen | `jbig2.exe` Net / Wall | `jbig2-wic` Gen (MP) | `jbig2-wic` Sym (MP) | Speedup (Gen / Sym vs. Net) |
|---|---:|---:|---:|---:|---:|
| **Test (7 Real-World)** | 669.5 KB | 463.5 / 511.4 ms | 669.5 KB (37.8 ms) | **646.6 KB** (97.6 ms) | **12.3x / 4.7x** |
| **01_scans (40 FUNSD)** | 276.3 KB | 350.2 / 623.9 ms | 276.2 KB (11.4 ms) | **276.2 KB** (54.7 ms) | **30.7x / 6.4x** |
| **02_text_mixed (20)** | 343.8 KB | 583.1 / 719.9 ms | 343.4 KB (16.8 ms) | **165.6 KB** (58.9 ms) | **34.7x / 9.9x** |
| **03_graphics (15)** | 78.0 KB | 231.7 / 334.3 ms | 77.5 KB (8.9 ms) | **77.5 KB** (11.2 ms) | **26.0x / 20.7x** |
| **04_photos (15 Kodak)** | 943.9 KB | 282.6 / 385.3 ms | 943.5 KB (32.7 ms) | **943.5 KB** (63.3 ms) | **8.6x / 4.5x** |
| **05_edgecases (10)** | 164.4 KB *(9/10)* | 101.9 / 163.5 ms | 163.7 KB (18.4 ms) | **153.0 KB** (19.3 ms) | **5.5x / 5.3x** |
| **TOTAL (107 Files)** | **2,475.9 KB** *(106/107)* | **2,013.1 / 2,738.3 ms** | **2,473.8 KB (126.0 ms)** | **2,262.4 KB (305.0 ms)** | **16.0x / 6.6x** |

### Key Reference Findings
- **Single-page parity (7 Real-World Scans):** Even without multi-page parallelism, `jbig2-wic` single-page Generic (`133.3 ms`) is **3.5x faster** than `jbig2.exe` net (`463.5 ms`), and single-page Symbol (`172.2 ms`) is **2.7x faster** while saving 1.8% bytes.
- **TPGD (`jbig2.exe -d`):** Yields identical total size (`2,475.9 KB`) in `2,328.5 ms` wall-clock time.
- **Lossless symbol coding unavailable in `jbig2enc`:** Running `jbig2.exe -s -r` exits immediately with *"Refinement broke in recent releases since it's rarely used"*.
- **Edge-case crash in `jbig2enc`:** `jbig2.exe` aborts with `rc=3` on `edge_03_1x1_black.png` (both default and `-d`), whereas `jbig2-wic` passes **107/107** files with **0-bit diff**.

---

## v2.4.0: LossySafe Symbol Coding (`flags=6`)

Verified on real 300-DPI scans plus external Leptonica & Tesseract test pages (structural full-page XOR check: **0** 2x2 error blocks on 12/13 documents):

| Document | Generic | Lossless | **LossySafe** | vs. Generic | vs. Lossless | 2x2 Blocks | Time |
|---|---:|---:|---:|---:|---:|---:|---:|
| **7-Page Scans (Multi-Page)** | 669.5 KB | 646.6 KB | **606.0 KB** | **-9.5%** | **-6.3%** | — | 137.2 ms |
| `phototest.png` (Tesseract) | 2,871 B | 1,930 B | **1,394 B** | **-51.4%** | **-27.8%** | **0** | 1.3 ms |
| `Vorlage3_fs.png` | 30,902 B | 23,139 B | **17,998 B** | **-41.8%** | **-22.2%** | **0** | 15.4 ms |
| `eurotext.png` (Tesseract) | 6,739 B | 6,739 B | **3,958 B** | **-41.3%** | **-41.3%** | **0** | 3.2 ms |
| `feyn.png` (Leptonica) | 83,713 B | 83,713 B | **49,969 B** | **-40.3%** | **-40.3%** | **0** | 45.6 ms |
| `EON.png` | 26,506 B | 24,166 B | **17,257 B** | **-34.9%** | **-28.6%** | **0** | 12.8 ms |
| `feyn-fract.png` (Leptonica) | 9,175 B | 9,175 B | **6,363 B** | **-30.6%** | **-30.6%** | **0** | 4.1 ms |
| `rabi.png` (Leptonica) | 129,436 B | 129,436 B | **96,542 B** | **-25.4%** | **-25.4%** | **0** | 49.0 ms |

## PDF Stream Export & Halftone Region (v2.5.0)

### PDF-Embedded Export (`/JBIG2Globals`, Zero-Copy)
- **7-Page Scan Set (300 DPI):** ~100–120 ms total encoding time with separated `/JBIG2Globals` dictionary.
- **Verification:** 0-bit diff in lossless mode; direct PDF rendering verified in Chrome/Edge.

### Halftone Region (`flags=8`, Segments 16 & 23)
| Test Image | Generic | Halftone (`flags=8`) | vs. Generic | Time | Diff |
|---|---:|---:|---:|---:|---|
| `synth_cell_4x4.png` (512x512) | 1,205 B | **217 B** | **-82.0%** | 2.2 ms | 0-bit |
| `kodim11..15_bayer4.png` (Pixel-Bayer) | 17–46 KB | Auto-Fallback | +0.0% | 7.0–9.8 ms | 0-bit |
