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

## Benchmark (1-bit Floyd-Steinberg Scans, 5 Documents)
| Metric | 1-bit PNG | `jbig2.exe -p` | `jbig2_wic.dll` |
|---|---:|---:|---:|
| Total Size | 1,234.6 KB | 613.4 KB | **613.6 KB (−50.3% vs PNG)** |
| Encode Time | — | 429.9 ms | **110.6 ms (3.9× faster)** |

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
