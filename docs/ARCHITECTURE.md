# Architecture & Encoding Pipeline

`jbig2-wic` is a C++20 lossless JBIG2 encoder (ITU-T T.88) and native Windows Imaging Component (WIC) codec.
Every encoded page is guaranteed to be **100% bit-exact** after decoding.

---

## 1. High-Level Pipeline (`EncodeLosslessDocument`)

When symbol coding is enabled, generic encoding and symbol analysis execute **concurrently** to hide CCL and clustering latency behind the MQ generic coder:

    pages[0..N-1]
      |---> [Generic MQ Worker(s)] -----------> gen_payloads[0..N-1] ----+
      |      (1 thread on 1 page, <= hw/2 threads on N pages)            |
      |                                                                  +--> [Cost & 1% Threshold] --> JBIG2 Bitstream
      +---> [BuildDocumentSymbolPlan] -> active pages -> [Residual MQ] --+
             1. Parallel Run-Length 4-CCL (with dither early-abort)
             2. Deterministic 64-bit Hamming clustering (+/-1 px shift)
             3. Per-page early-exit & global Segment 0 pruning
             4. Parallel Text Region (GRRD T1) & residual bitmaps

### Threading & Safety Guarantees
- **RAII Thread Management:** Uses C++20 `std::jthread` so worker threads always join cleanly on scope exit.
- **Bounded Concurrency:** Generic workers are capped at `hw / 2` threads during multi-page overlap to prevent 2x CPU oversubscription against CCL workers.
- **No `std::vector<bool>` Races:** `plan.page_has_text` is populated sequentially before parallel text-region workers start; workers write exclusively to disjoint per-page buffers.
- **Memory Discipline:** Run-length buffers are thread-local and discarded before clustering; residual bitmaps are released (`clear()` + `shrink_to_fit()`) immediately after residual MQ encoding.

---

## 2. Run-Length 4-CCL & Dither Early-Abort (`src/jbig2_symbol_encoder.cpp`)

Instead of allocating a full-frame `uint32_t` label array (~35 MB per 300 DPI page), `ExtractGlyphsForPage` scans each row into half-open black runs `[x0, x1)` using C++20 `<bit>` (`std::countl_zero`, `std::countl_one`) and merges 4-connected runs via a two-pointer sweep with path-compressed Union-Find.

- **~20x Lower RAM Footprint:** Eliminates page-fault contention across parallel worker threads.
- **Dither / Noise Early-Abort:** Each row counts 1-px black runs (`row_1px`). On pages with `w >= 256`, a row is flagged as dither/noise when `row_1px > 8%` of page width and `>= 45%` of all runs in that row. If flagged rows exceed **50% of total page height**, extraction aborts immediately - avoiding wasted CCL/clustering work on dithered photos, checkerboards, and random noise while preserving mixed text+photo pages.
- **Glyph Filter:** Keeps components with `count >= 22`, `6 <= w <= 64`, `8 <= h <= 72`, and `w * h >= 72`, stored as bit-packed `std::array<uint64_t, 72>` rows.

---

## 3. Deterministic Clustering & Lossless GRRD T1 Refinement

1. **Deterministic Ordering:** Per-page glyph lists are concatenated in strict page order (`0..N-1`) before clustering so the output bitstream is 100% deterministic across runs.
2. **64-Bit Hamming Distance:** Candidates are bucketed by `(w, h)` and matched against `(w+/-1, h+/-1)` prototypes using `std::popcount` with base centering plus `{(+/-1, 0), (0, +/-1)}` refinement shifts.
3. **Per-Page Early-Exit:** A page remains active for symbol coding only if it has at least `32` matched glyphs (`kMinPageInstances`), at least `35%` of its extracted candidates matched (`kMinPageMatchPct`), and at most `88%` requiring refinement (`kMaxPageRefinedPct`).
4. **Global Dictionary Pruning (Segment 0):** Only symbol groups that still meet the reuse criterion across *active* pages are written into Segment 0 (Template-0 Generic Region, no TPGDON).
5. **100% Lossless Text Regions:** Non-exact matches are encoded via **GRRD Template 1** (ITU-T T.88 Figure 13, 10-bit context, 0 AT bytes). Extracted glyph pixels are cleared from the page bitmap to form the residual image, which is OR-combined at decode time.

---

## 4. Per-Page Cost Selection (`src/jbig2_mq_encoder.cpp`)

For every active page `i`, the encoder compares `gen_size = gen_payloads[i].size()` against `sym_page_cost = text_payloads[i].size() + 12 + res_payloads[i].size()`.
Symbol coding is only selected for page `i` if `sym_page_cost + gen_size / 100 < gen_size` (**>= 1% net saving**). Segment 0 is emitted only if the sum of per-page savings exceeds `dict_payload.size() + 12`. Otherwise, the encoder falls back cleanly to the already-computed generic payload.
