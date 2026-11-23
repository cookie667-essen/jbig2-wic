# JBIG2-WIC

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
| Encode Time | — | 401.6 ms | **153.5 ms (2.6× faster)** |

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

## License
- Codec & MQ Encoder: **BSD-3-Clause License**
- Vendored PDFium JBIG2/Fax decoder: **BSD-3-Clause** (see `third_party/pdfium/LICENSE`)
