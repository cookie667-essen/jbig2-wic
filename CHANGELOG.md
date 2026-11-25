# Changelog

All notable changes to **jbig2-wic** are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
