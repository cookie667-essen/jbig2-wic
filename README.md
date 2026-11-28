# jbig2-tools

> [!NOTE]
> [![Latest Release](https://img.shields.io/github/v/release/cookie667-essen/jbig2-tools?label=Latest%20Release)](https://github.com/cookie667-essen/jbig2-tools/releases/latest) — Zero-copy PDF `/JBIG2Globals` stream export, lossless 4x4 Halftone Region coding (`flags=8`), optional `LossySafe` symbol coding (4-stage Xerox guard) & 0-bit lossless GRRD T1 symbol coding.


A fast, zero-dependency, cross-platform **JBIG2 (ITU-T T.88)** toolkit for **Windows x64** (`jbig2_wic.dll` WIC codec) and **Linux x64** (`libjbig2.so`, `jbig2-cli`, Freedesktop thumbnailer).

## Features
- **Secure Decoder:** Built on vendored PDFium (`core/fxcodec/jbig2`) including FaxG4 (MMR) support and DoS/OOM protection.
- **Windows Explorer Thumbnails:** Native thumbnail generation for `.jb2` / `.jbig2` and compatibility with classic Windows Photo Viewer.
- **Fast Lossless MQ Encoder:** Custom ITU-T T.88 Generic Region encoder (Template 0 + TPGDON, 8-pixel byte-chunking) with multi-page support (`IWICBitmapEncoder`).
- **Optional LossySafe Symbol Mode (`flags=6`):** Opt-in `SBREFINE=0` symbol coding with a 4-stage Xerox-substitution guard (small-glyph exact match, hole topology check, 2x2 XOR block & 3x3 cluster filter) — up to **-51% vs. Generic** on 300-DPI scans with **0** structural 2x2 error blocks.
- **Lossless Halftone Region Mode (`flags=8`):** ITU-T T.88 Segments 16 & 23 (4x4 pattern dictionary + Popcount-sorted Gray-code bitplanes) with L1-bitset extraction and automatic Generic fallback (> 64 patterns) — up to **-82.0% vs. Generic** on periodic 4x4 cell halftones with **0-bit diff**.
- **Zero-Copy C API & PDF Stream Export:** Exported `jbig2_encode_1bpp`, `jbig2_encode_multipage_1bpp`, and `jbig2_encode_pdf_1bpp` (ISO 32000-1 §7.4.7 `/JBIG2Globals` + per-page streams without file header/EOF) for direct in-memory encoding from Python (`ctypes`) or C/C++.
- **Zero-Dependency CLI (`jbig2-cli`):** Cross-platform command-line tool (`info`, `decode` to PBM/PNG, `thumb`, `encode` to multi-page `.jb2` or `/JBIG2Globals` `.pdf`).
- **Linux Shared Library & Thumbnails:** Native `libjbig2.so` (`ctypes`/C API) plus Freedesktop `.thumbnailer` & MIME integration for Nautilus, Thunar, Nemo, and Caja.
- **Single-File Windows DLL:** Statically linked CRT (`/MT`) — no VC++ Redistributable required.

## Benchmarks (v2.5.0)

| Dataset (Multi-Page) | Orig PNG | `jbig2.exe` Gen (Net) | `jbig2-wic` Gen | `jbig2-wic` Sym | vs. PNG | Speedup vs. `jbig2.exe` |
|---|---:|---:|---:|---:|---:|---:|
| **7 Real-World Scans** | 1,390.7 KB | 669.5 KB (463.5 ms) | 669.5 KB (37.8 ms) | **646.6 KB** (97.6 ms) | **-53.5%** | **12.3x** Gen / **4.7x** Sym |
| **107-File Corpus** | 4,495.4 KB | 2,475.9 KB\* (2,013.1 ms) | 2,473.8 KB (126.0 ms) | **2,262.4 KB** (305.0 ms) | **-49.7%** | **16.0x** Gen / **6.6x** Sym |

\* `jbig2.exe` (`jbig2enc`) fed uncompressed P4 PBM from OS cache (6.8 ms/call process spawn subtracted). It passes 106/107 files (crashes with `rc=3` on `1x1` black) and its lossless symbol mode (`-s -r`) is broken/disabled upstream. `jbig2-wic` passes **107/107** with **0-bit diff**. See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for the full breakdown.

**LossySafe (`flags=6`):** Cuts the 7-page scan set further to **606.0 KB** (-56.4% vs. PNG / -9.5% vs. Generic in 137.2 ms) and achieves **-25% to -51% vs. Generic** on external 200-300 DPI Leptonica/Tesseract scans with **0** 2x2 structural error blocks.

**PDF Export & Halftone (`flags=8`):** Zero-copy `jbig2_encode_pdf_1bpp` encodes the 7-page 300-DPI scan set in **~100–120 ms** (browser-verified in Chrome/Edge). Lossless 4x4 Halftone coding achieves **-82.0% vs. Generic** (1,205 B -> 217 B in 2.2 ms) on periodic 4x4 cell halftones while falling back cleanly to Generic in 7–10 ms on non-periodic Bayer photos.

## Installation (Windows x64)
Run PowerShell **as Administrator**:
```powershell
.\scripts\install.ps1
```

## Installation (Linux x64)
**Debian / Ubuntu / Mint (`.deb`):**
```bash
sudo apt install ./jbig2-tools_*_amd64.deb
```

**Generic Linux tarball or from source:**
```bash
sudo ./install-linux.sh          # system-wide (/usr/local)
# or user-local (~/.local):
./install-linux.sh --user
```

## Build from Source
**Windows (MSVC):**
```powershell
cmake -B build -A x64
cmake --build build --config Release
```

**Linux (GCC / Ninja):**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
JBIG2_BUILD_DIR=build python3 tests/ci_selftest.py
```

## Documentation

- [Architecture & Encoding Pipeline](docs/ARCHITECTURE.md) — MQ coder, Run-Length 4-CCL, GRRD T1 refinement, std::jthread overlap
- [Benchmarks](docs/BENCHMARKS.md) — Progression (v2.1 to v2.3) and 107-file multi-category corpus (-49.7% vs. PNG, 0-bit diff)
- [Changelog](CHANGELOG.md) — Release history

## License
- Codec & MQ Encoder: **BSD-3-Clause License**
- Vendored PDFium JBIG2/Fax decoder: **BSD-3-Clause** (see `third_party/pdfium/LICENSE`)
