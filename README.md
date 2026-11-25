# JBIG2-WIC

> [!NOTE]
> [![Latest Release](https://img.shields.io/github/v/release/cookie667-essen/jbig2-wic?label=Latest%20Release)](https://github.com/cookie667-essen/jbig2-wic/releases/latest) — Overlapped generic/symbol encoding (`std::jthread`), dither CCL early-abort & lossless GRRD T1 symbol coding (1.06×–1.29× generic on single pages, ~100 ms for 7 pages, 0-bit diff across 107 test files).


A standalone, zero-dependency **Windows Imaging Component (WIC)** codec (`jbig2_wic.dll`) for `.jb2` and `.jbig2` files on Windows 10/11.

## Features
- **Secure Decoder:** Built on vendored PDFium (`core/fxcodec/jbig2`) including FaxG4 (MMR) support and DoS/OOM protection.
- **Windows Explorer Thumbnails:** Native thumbnail generation for `.jb2` / `.jbig2` and compatibility with classic Windows Photo Viewer.
- **Fast Lossless MQ Encoder:** Custom ITU-T T.88 Generic Region encoder (Template 0 + TPGDON, 8-pixel byte-chunking) with multi-page support (`IWICBitmapEncoder`).
- **Zero-Copy C API:** Exported `jbig2_encode_1bpp` and `jbig2_encode_multipage_1bpp` for direct in-memory encoding from Python (`ctypes`) or C/C++.
- **Single-File DLL:** Statically linked CRT (`/MT`) — no VC++ Redistributable required.

## Benchmarks (v2.3.0, 0-Bit Lossless)

| Dataset (Multi-Page) | Orig PNG | `jbig2.exe` Gen (Net) | `jbig2-wic` Gen | `jbig2-wic` Sym | vs. PNG | Speedup vs. `jbig2.exe` |
|---|---:|---:|---:|---:|---:|---:|
| **7 Real-World Scans** | 1,390.7 KB | 669.5 KB (463.5 ms) | 669.5 KB (37.8 ms) | **646.6 KB** (97.6 ms) | **-53.5%** | **12.3x** Gen / **4.7x** Sym |
| **107-File Corpus** | 4,495.4 KB | 2,475.9 KB\* (2,013.1 ms) | 2,473.8 KB (126.0 ms) | **2,262.4 KB** (305.0 ms) | **-49.7%** | **16.0x** Gen / **6.6x** Sym |

\* `jbig2.exe` (`jbig2enc`) fed uncompressed P4 PBM from OS cache (6.8 ms/call process spawn subtracted). It passes 106/107 files (crashes with `rc=3` on `1x1` black) and its lossless symbol mode (`-s -r`) is broken/disabled upstream. `jbig2-wic` passes **107/107** with **0-bit diff**. See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for the full breakdown.

## Installation (Windows x64)
Run PowerShell **as Administrator**:
```powershell
.\scripts\install.ps1
```

## Build from Source
```powershell
cmake -B build -A x64
cmake --build build --config Release
```

## Documentation

- [Architecture & Encoding Pipeline](docs/ARCHITECTURE.md) — MQ coder, Run-Length 4-CCL, GRRD T1 refinement, std::jthread overlap
- [Benchmarks](docs/BENCHMARKS.md) — Progression (v2.1 to v2.3) and 107-file multi-category corpus (-49.7% vs. PNG, 0-bit diff)
- [Changelog](CHANGELOG.md) — Release history

## License
- Codec & MQ Encoder: **BSD-3-Clause License**
- Vendored PDFium JBIG2/Fax decoder: **BSD-3-Clause** (see `third_party/pdfium/LICENSE`)
