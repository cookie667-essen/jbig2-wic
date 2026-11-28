# Changelog

All notable changes to **jbig2-wic** are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.1] - 2026-11-28

### Added
- **Debian/Ubuntu `.deb` Package**: Automated `jbig2-tools_<ver>_amd64.deb` built in `debian:bookworm` for broad glibc compatibility (`>= 2.36`).
- **Public C Header (`include/jbig2_c_api.h`)**: Clean C-API declarations for `libjbig2.so` / `jbig2_wic.dll`.
- **CLI `--version` & `stdin` Pipe (`-`)**: Pipe P4 PBM streams directly into `jbig2-cli encode - -o out.jb2`.
- **Corrupt-Input & Pipe Self-Tests**: Verified under ASan + UBSan.

### Fixed
- **LossySafe Small-Glyph Guard**: Symmetrically enforces exact matching when either glyph or prototype is smaller than `10x12` px; enables 2-member groups in Step 4.
- **C-API & WIC Hardening**: Added `stride` validation in `jbig2_c_api.cpp`, shared internal `jbig2_file_parser.h` header, `std::jthread` exception safety, parallel multi-page halftone encoding, and proper `E_ACCESSDENIED` reporting in `wic_registry.cpp`.

## [3.0.0] - 2026-11-27

### Added
- **Linux x64 Support (`libjbig2.so`)**: Unified C++20 core & C-API (`src/jbig2_c_api.cpp`) compiling natively with GCC/Ninja alongside Windows MSVC.
- **Zero-Dependency CLI (`jbig2-cli`)**: Cross-platform CLI for `info`, `decode` (P4 PBM / 1-bit PNG), `thumb` (antialiased 8-bit PNG), and `encode` (multi-page `.jb2` and `/JBIG2Globals` `.pdf`).
- **Linux Desktop Integration**: Freedesktop `.thumbnailer`, Shared-MIME-Info XML (`*.jb2`/`*.jbig2` magic bytes), and `linux/install-linux.sh` (`--user` or system-wide).
- **Dual-Platform CI**: Added `ubuntu-latest` build, selftest, and release packaging (`jbig2-tools-linux-x64.tar.gz` + `jbig2-tools-win64.zip`).

### Changed
- **Project Renamed to `jbig2-tools`**: Reflects cross-platform scope (Windows WIC codec `jbig2_wic.dll` filename preserved for backward compatibility).

## [2.5.0] - 2026-11-26

### Added
- **PDF-Embedded Stream Export (`jbig2_encode_pdf_1bpp`)**: ISO 32000-1 §7.4.7-konforme Ausgabe mit getrenntem `/JBIG2Globals`-Stream und seitenweisen Streams ohne File-Header/EOF, inkl. Zero-Copy-Pipeline (`Bitmap1bppView`).
- **Lossless Halftone Region Encoder (`flags=8`, Segmente 16 & 23)**: 4×4 Pattern Dictionary + Popcount-sortierte Gray-Code-Bitebenen mit L1-Bitset, Nibble-Fast-Path und automatischem Generic-Fallback bei > 64 Mustern (−82 % auf 4×4-Zellenrastern bei 0 Bit Abweichung).

## [v2.4.0] - 2026-11-26

### Added
- **LossySafe Symbol Coding (`flags = 6` / `EncodeOptions::lossy_symbol`):** Optional lossy text-region mode (`SBREFINE = 0`) that eliminates refinement overhead on scanned documents while keeping unmatched glyphs, line graphics, and dithered regions 100% lossless in the residual layer.
- **4-Stage Xerox-Substitution Guard:**
  1. *Small-Glyph Guard:* Glyphs smaller than `10x12 px` match only at 0-bit difference (direct `memcmp`).
  2. *Topology Check:* Parallel 64-bit flood-fill (`CountHoles`) enforces identical enclosed hole counts (prevents `8` vs. `6/9/0` vs. `3/5`).
  3. *2x2 XOR Block Filter:* Rejects any match producing a solid 2x2 difference block.
  4. *3x3 Cluster & Boundary Guard:* Rejects 3x3 windows with >= 5 differing pixels (with popcount fast-path) and prevents prototype clamping shifts at page edges.
- **CI Coverage (`tests/ci_selftest.py`):** Added automated single-page and multi-page `LossySafe` verification including full-page `2x2-Blk == 0` structural diff checks.

### Benchmarks
- **7 Real-World Scans (Multi-Page):** `669.5 KB` (Generic) -> `646.6 KB` (Lossless) -> **`606.0 KB`** (LossySafe, **-9.5% vs. Generic / -56.4% vs. PNG**) in **137.2 ms**.
- **External 200-300 DPI Scans (Leptonica & Tesseract):** **-25.4% to -51.4%** vs. Generic (`phototest` -51.4%, `eurotext` -41.3%, `feyn` -40.3%, `feyn-fract` -30.6%, `rabi` -25.4%) with **0** 2x2 error blocks.

## [v2.3.0] - 2026-11-25

### Performance
- **Overlapped Generic & Symbol Planning (`std::jthread`):** Generic MQ encoding now runs concurrently with `BuildDocumentSymbolPlan` using a bounded thread budget (`hw / 2` for multi-page generic, 1+1 threads on single pages). Single-page symbol overhead on dithered scans dropped to **1.06x-1.12x** of pure generic encoding.
- **Dither / Noise CCL Early-Abort:** `ExtractGlyphsForPage` tracks per-row 1-px run density and aborts early once >50% of image height consists of high-frequency dither or noise rows. Multi-page photo corpus (`04_photos`, 15 Kodak images) sped up by **3.1x** (`196.4 ms -> 63.3 ms`).
- **Immediate Residual Memory Release:** Per-page residual bitmaps are freed right after residual MQ encoding finishes to minimize peak RAM usage on large multi-page jobs.

### Verified
- **0-bit lossless roundtrip** verified across **107 test images** (real scans, synthetic text/mixed pages, line graphics, dithered photos, and stress edge cases): **-49.7% vs. 1-bit PNG** (`4.50 MB -> 2.26 MB`).

## [v2.2.0] - 2026-11-24

### Added
- **Lossless Symbol Coding (ITU-T T.88):** Global multi-page symbol dictionary (Segment 0) combined with Template-1 generic refinement regions (**GRRD T1**, 10-bit context, 0 AT bytes) and OR-combined residual generic regions.
- **Per-Page Early-Exit & Dictionary Pruning:** Pages below the cluster match threshold (`kMinPageMatchPct = 35`) exit directly after clustering; Segment 0 is pruned to symbols actively referenced by surviving text pages.
- **Relative Saving Threshold (1%):** Symbol coding is selected per page only when it saves at least 1% over pure generic coding, avoiding decoder overhead for negligible byte gains.

### Performance
- Replaced full-frame `uint32_t` label buffers with **multi-threaded Run-Length 4-CCL** (cuts per-page CCL memory by ~20x).
- Parallelized text-region & residual bitmap generation without `std::vector<bool>` data races.
- Multi-page encoding speedup on the 7-page reference set: Generic `132.3 ms -> 44.0 ms` (**3.0x**), Symbol `400.3 ms -> 113.7 ms` (**3.5x**).

### Fixed
- Resolved `CheckedNumeric` overflow edge case in `fx_safe_types.h`.
- Extended `ci_selftest.py` / `encoder_test.cpp` with multi-page `--compare` bit-exact verification.

## [v2.1.0]

### Performance
- MQ arithmetic encoder hot-path tuning and MSVC LTCG optimization (**3.9x faster** than `jbig2.exe`, 110.6 ms for 5 pages).

### Added
- Automated CI self-test suite (`tests/ci_selftest.py`).
