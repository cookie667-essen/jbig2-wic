#include "jbig2_mq_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <atomic>
#include <thread>


namespace jbig2 {
namespace {

using detail::MqEncoder;
using detail::PushBE32;
using detail::WriteSegHdr;
using detail::WriteSegHdrRef1;

struct AtCoord { int8_t dx; int8_t dy; };
constexpr std::array<AtCoord, 4> kDefaultAt = {{{3, -1}, {-3, -1}, {2, -2}, {-2, -2}}};

inline int GetBit(const uint8_t* row, uint32_t x, uint32_t w) {
    return (x >= w) ? 0 : (((row[x >> 3] >> (7 - (x & 7))) & 1) ^ 1);
}
inline int GetBitXY(const uint8_t* base, uint32_t stride, uint32_t w, int64_t x, int64_t y) {
    if (x < 0 || y < 0 || x >= static_cast<int64_t>(w)) return 0;
    const uint8_t* r = base + static_cast<size_t>(y) * stride;
    return (((r[x >> 3] >> (7 - (x & 7))) & 1) ^ 1);
}
bool RowsEqual(const uint8_t* a, const uint8_t* b, uint32_t w) {
    const uint32_t fb = w >> 3;
    if (fb > 0 && std::memcmp(a, b, fb) != 0) return false;
    const uint32_t rem = w & 7;
    return rem == 0 || (((a[fb] ^ b[fb]) & static_cast<uint8_t>(0xFFu << (8 - rem))) == 0);
}
bool IsFixedT0(int dx, int dy) {
    return (dy == 0 && dx >= -4 && dx <= -1) || (dy == -1 && dx >= -2 && dx <= 2) || (dy == -2 && dx >= -1 && dx <= 1);
}

std::array<AtCoord, 4> TuneAT(const Bitmap1bppView& bmp) {
    if (bmp.width < 32 || bmp.height < 16) return kDefaultAt;
    const uint32_t w = bmp.width, h = bmp.height;
    const uint32_t rs = std::max<uint32_t>(1, h / 24), cs = std::max<uint32_t>(1, w / 256);
    struct Cand { AtCoord c; int64_t score; };
    std::vector<Cand> cands; cands.reserve(80);

    for (int dy = 0; dy >= -4; --dy) {
        for (int dx = (dy == 0 ? -12 : -8); dx <= (dy == 0 ? -5 : 8); ++dx) {
            if (IsFixedT0(dx, dy)) continue;
            int64_t same = 0, total = 0;
            for (uint32_t y = 4; y < h; y += rs) {
                const uint8_t* r0 = bmp.pixels + static_cast<size_t>(y) * bmp.stride;
                const uint8_t* rd = bmp.pixels + static_cast<size_t>(y + dy) * bmp.stride;
                const uint32_t xs = static_cast<uint32_t>(std::max(0, -dx));
                const uint32_t xe = static_cast<uint32_t>(std::min<int64_t>(w, static_cast<int64_t>(w) - dx));
                for (uint32_t x = xs; x < xe; x += cs) {
                    same += (GetBit(r0, x, w) == GetBit(rd, static_cast<uint32_t>(static_cast<int64_t>(x) + dx), w));
                    total++;
                }
            }
            if (total > 0) cands.push_back({ {static_cast<int8_t>(dx), static_cast<int8_t>(dy)}, std::abs(2 * same - total) });
        }
    }
    if (cands.size() < 4) return kDefaultAt;
    std::partial_sort(cands.begin(), cands.begin() + 4, cands.end(),
                      [](const Cand& a, const Cand& b) { return a.score > b.score; });
    auto scoreOf = [&](AtCoord t) -> int64_t {
        for (const auto& c : cands) if (c.c.dx == t.dx && c.c.dy == t.dy) return c.score;
        return 0;
    };
    int64_t dsum = 0, bsum = 0;
    for (int i = 0; i < 4; ++i) { dsum += scoreOf(kDefaultAt[i]); bsum += cands[i].score; }
    if (bsum <= dsum + (dsum / 25)) return kDefaultAt;
    return { cands[0].c, cands[1].c, cands[2].c, cands[3].c };
}

std::vector<uint8_t> EncodeGenericRegionPayload(const Bitmap1bppView& bmp, bool adaptive_templates, uint8_t* shared_ctx) {
    const uint32_t w = bmp.width, h = bmp.height;
    std::vector<uint8_t> blank(bmp.stride, 0xFF);
    const auto at = adaptive_templates ? TuneAT(bmp) : kDefaultAt;
    const bool def_at = std::equal(at.begin(), at.end(), kDefaultAt.begin(),
                                   [](AtCoord a, AtCoord b) { return a.dx == b.dx && a.dy == b.dy; });

    std::memset(shared_ctx, 0, 65536);
    MqEncoder mq(std::max<size_t>(1024, (static_cast<size_t>(w) * h) / 32));
    int ltp = 0;
    const uint32_t safe_bytes = (w > 4) ? ((w - 4) >> 3) : 0;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* r0 = bmp.pixels + static_cast<size_t>(y) * bmp.stride;
        const uint8_t* r1 = (y >= 1) ? (bmp.pixels + static_cast<size_t>(y - 1) * bmp.stride) : blank.data();
        const uint8_t* r2 = (y >= 2) ? (bmp.pixels + static_cast<size_t>(y - 2) * bmp.stride) : blank.data();

        const int same = RowsEqual(r0, r1, w) ? 1 : 0;
        mq.EncodeBit(shared_ctx, 0x9B25, same != ltp);
        ltp = same;
        if (ltp) continue;

        if (def_at) {
            uint32_t c2 = (GetBit(r2, 0, w) << 2) | (GetBit(r2, 1, w) << 1) | GetBit(r2, 2, w);
            uint32_t c1 = (GetBit(r1, 0, w) << 3) | (GetBit(r1, 1, w) << 2) | (GetBit(r1, 2, w) << 1) | GetBit(r1, 3, w);
            uint32_t c0 = 0;

            for (uint32_t bx = 0; bx < safe_bytes; ++bx) {
                const uint32_t w0 = static_cast<uint8_t>(~r0[bx]);
                const uint32_t w1 = (static_cast<uint32_t>(static_cast<uint8_t>(~r1[bx])) << 8) | static_cast<uint8_t>(~r1[bx + 1]);
                const uint32_t w2 = (static_cast<uint32_t>(static_cast<uint8_t>(~r2[bx])) << 8) | static_cast<uint8_t>(~r2[bx + 1]);

                if ((w0 | w1 | w2 | c0 | c1 | c2) == 0 && mq.TryEncode8Same(shared_ctx, 0x0000, 0)) continue;
                if (w0 == 0xFFu && w1 == 0xFFFFu && w2 == 0xFFFFu &&
                    c0 == 0x0Fu && c1 == 0x7Fu && c2 == 0x1Fu && mq.TryEncode8Same(shared_ctx, 0xFFFF, 1)) continue;

                for (int k = 0; k < 8; ++k) {
                    const int d = static_cast<int>((w0 >> (7 - k)) & 1u);
                    mq.EncodeBit(shared_ctx, static_cast<uint16_t>(((c2 & 0x1Fu) << 11) | ((c1 & 0x7Fu) << 4) | (c0 & 0x0Fu)), d);
                    c2 = ((c2 << 1) | ((w2 >> (12 - k)) & 1u)) & 0x1Fu;
                    c1 = ((c1 << 1) | ((w1 >> (11 - k)) & 1u)) & 0x7Fu;
                    c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0x0Fu;
                }
            }
            for (uint32_t x = safe_bytes << 3; x < w; ++x) {
                const int d = GetBit(r0, x, w);
                mq.EncodeBit(shared_ctx, static_cast<uint16_t>(((c2 & 0x1Fu) << 11) | ((c1 & 0x7Fu) << 4) | (c0 & 0x0Fu)), d);
                c2 = ((c2 << 1) | GetBit(r2, x + 3, w)) & 0x1Fu;
                c1 = ((c1 << 1) | GetBit(r1, x + 4, w)) & 0x7Fu;
                c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0x0Fu;
            }
        } else {
            uint32_t c2 = (GetBit(r2, 0, w) << 1) | GetBit(r2, 1, w);
            uint32_t c1 = (GetBit(r1, 0, w) << 2) | (GetBit(r1, 1, w) << 1) | GetBit(r1, 2, w);
            uint32_t c0 = 0;
            const int64_t iy = static_cast<int64_t>(y);
            for (uint32_t x = 0; x < w; ++x) {
                const int64_t ix = static_cast<int64_t>(x);
                const uint32_t a0 = GetBitXY(bmp.pixels, bmp.stride, w, ix + at[0].dx, iy + at[0].dy);
                const uint32_t a1 = GetBitXY(bmp.pixels, bmp.stride, w, ix + at[1].dx, iy + at[1].dy);
                const uint32_t a2 = GetBitXY(bmp.pixels, bmp.stride, w, ix + at[2].dx, iy + at[2].dy);
                const uint32_t a3 = GetBitXY(bmp.pixels, bmp.stride, w, ix + at[3].dx, iy + at[3].dy);
                const int d = GetBit(r0, x, w);
                mq.EncodeBit(shared_ctx, static_cast<uint16_t>((a3 << 15) | ((c2 & 7u) << 12) | (a2 << 11) |
                                                               (a1 << 10) | ((c1 & 0x1Fu) << 5) | (a0 << 4) | (c0 & 0xFu)), d);
                c2 = ((c2 << 1) | GetBit(r2, x + 2, w)) & 7u;
                c1 = ((c1 << 1) | GetBit(r1, x + 3, w)) & 0x1Fu;
                c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0xFu;
            }
        }
    }

    const auto mq_bytes = mq.Finish();
    std::vector<uint8_t> payload;
    payload.reserve(26u + mq_bytes.size());
    PushBE32(payload, w); PushBE32(payload, h); PushBE32(payload, 0); PushBE32(payload, 0);
    payload.push_back(0x00); payload.push_back(0x08);
    for (int i = 0; i < 4; ++i) {
        payload.push_back(static_cast<uint8_t>(at[i].dx));
        payload.push_back(static_cast<uint8_t>(at[i].dy));
    }
    payload.insert(payload.end(), mq_bytes.begin(), mq_bytes.end());
    return payload;
}

template <typename Fn>
void RunParallelWithCtx(size_t count, Fn&& fn) {
    if (count <= 1) {
        std::vector<uint8_t> ctx(65536, 0);
        for (size_t i = 0; i < count; ++i) fn(i, ctx.data());
        return;
    }
    const unsigned hw = std::thread::hardware_concurrency();
    const size_t n_threads = std::min<size_t>(count, hw > 0 ? hw : 4u);
    if (n_threads <= 1) {
        std::vector<uint8_t> ctx(65536, 0);
        for (size_t i = 0; i < count; ++i) fn(i, ctx.data());
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::jthread> workers;
    workers.reserve(n_threads - 1);
    auto worker = [&]() {
        std::vector<uint8_t> ctx(65536, 0);
        for (;;) {
            const size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) break;
            fn(i, ctx.data());
        }
    };
    for (size_t t = 1; t < n_threads; ++t) workers.emplace_back(worker);
    worker();
}
void WritePageInfo(std::vector<uint8_t>& file, uint32_t& seg, uint32_t page, const Bitmap1bppView& bmp) {
    WriteSegHdr(file, seg++, 48, page, 19);
    PushBE32(file, bmp.width); PushBE32(file, bmp.height);
    PushBE32(file, bmp.dpi_x > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_x / 0.0254)) : 0);
    PushBE32(file, bmp.dpi_y > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_y / 0.0254)) : 0);
    file.push_back(0x01); file.push_back(0x00); file.push_back(0x00);
}

} // namespace

namespace {
struct PreparedDocument {
    std::vector<std::vector<uint8_t>> gen_payloads;
    std::vector<std::vector<uint8_t>> res_payloads;
    std::vector<uint8_t> use_sym;
    std::vector<uint8_t> use_ht;
    std::vector<detail::PageHalftonePlan> ht_plans;
    detail::DocumentSymbolPlan plan;
    bool emit_global_dict = false;
    bool ok = false;
};
PreparedDocument PrepareDocument(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts) {
    PreparedDocument out;
    if (pages.empty()) return out;
    for (const auto& p : pages) {
        if (!p.pixels || p.width == 0 || p.height == 0 || p.stride < (p.width + 7) / 8 || p.pixel_bytes < static_cast<size_t>(p.stride) * p.height) return out;
    }
    out.gen_payloads.resize(pages.size());
    out.use_sym.assign(pages.size(), 0);
    out.use_ht.assign(pages.size(), 0);
    if (opts.halftone_coding) out.ht_plans.resize(pages.size());
    if (!opts.symbol_coding && !opts.lossy_symbol) {
        RunParallelWithCtx(pages.size(), [&](size_t i, uint8_t* ctx) { out.gen_payloads[i] = EncodeGenericRegionPayload(pages[i], opts.adaptive_templates, ctx); });
        if (opts.halftone_coding) {
            RunParallelWithCtx(pages.size(), [&](size_t i, uint8_t*) {
                out.ht_plans[i] = detail::BuildPageHalftonePlan(pages[i]);
                if (out.ht_plans[i].valid && out.ht_plans[i].dict_payload.size() + out.ht_plans[i].region_payload.size() + 24u < out.gen_payloads[i].size()) out.use_ht[i] = 1;
            });
        }
        out.ok = true; return out;
    }
    out.res_payloads.resize(pages.size());
    std::jthread gen_worker([&]() {
        if (pages.size() <= 1) {
            std::vector<uint8_t> ctx(65536, 0);
            out.gen_payloads[0] = EncodeGenericRegionPayload(pages[0], opts.adaptive_templates, ctx.data());
            return;
        }
        const unsigned hw = std::thread::hardware_concurrency();
        const size_t gen_threads = std::min<size_t>(pages.size(), std::max<size_t>(1u, (hw ? hw : 4u) / 2u));
        std::atomic<size_t> next_gen{0};
        auto worker = [&]() {
            std::vector<uint8_t> ctx(65536, 0);
            for (;;) {
                const size_t i = next_gen.fetch_add(1, std::memory_order_relaxed);
                if (i >= pages.size()) break;
                out.gen_payloads[i] = EncodeGenericRegionPayload(pages[i], opts.adaptive_templates, ctx.data());
            }
        };
        std::vector<std::jthread> pool; pool.reserve(gen_threads > 1 ? gen_threads - 1 : 0);
        for (size_t t = 1; t < gen_threads; ++t) pool.emplace_back(worker);
        worker();
    });
    out.plan = detail::BuildDocumentSymbolPlan(pages, opts.lossy_symbol);
    gen_worker.join();
    std::vector<size_t> res_pages;
    if (out.plan.valid) { res_pages.reserve(pages.size()); for (size_t i = 0; i < pages.size(); ++i) if (out.plan.page_has_text[i]) res_pages.push_back(i); }
    if (!res_pages.empty()) {
        RunParallelWithCtx(res_pages.size(), [&](size_t r_idx, uint8_t* ctx) {
            const size_t i = res_pages[r_idx];
            Bitmap1bppView rv = pages[i]; rv.pixels = out.plan.residuals[i].data(); rv.pixel_bytes = out.plan.residuals[i].size();
            out.res_payloads[i] = EncodeGenericRegionPayload(rv, opts.adaptive_templates, ctx);
            out.plan.residuals[i].clear(); out.plan.residuals[i].shrink_to_fit();
        });
    }
    int64_t total_saved = 0;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (out.plan.valid && out.plan.page_has_text[i]) {
            const size_t gs = out.gen_payloads[i].size();
            const size_t sc = out.plan.text_payloads[i].size() + 12u + out.res_payloads[i].size();
            if (sc + (gs / 100u) < gs) { out.use_sym[i] = 1; total_saved += static_cast<int64_t>(gs - sc); }
        }
    }
    out.emit_global_dict = out.plan.valid && total_saved > static_cast<int64_t>(out.plan.dict_payload.size() + 12u);
    if (opts.halftone_coding) {
        std::vector<size_t> ht_pages;
        ht_pages.reserve(pages.size());
        for (size_t i = 0; i < pages.size(); ++i) {
            if (!(out.emit_global_dict && out.use_sym[i])) ht_pages.push_back(i);
        }
        RunParallelWithCtx(ht_pages.size(), [&](size_t idx, uint8_t*) {
            const size_t i = ht_pages[idx];
            out.ht_plans[i] = detail::BuildPageHalftonePlan(pages[i]);
            if (out.ht_plans[i].valid && out.ht_plans[i].dict_payload.size() + out.ht_plans[i].region_payload.size() + 24u < out.gen_payloads[i].size()) out.use_ht[i] = 1;
        });
    }
    out.ok = true; return out;
}
} // namespace

std::vector<uint8_t> EncodeLosslessDocumentView(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts) {
    auto prep = PrepareDocument(pages, opts);
    if (!prep.ok) return {};
    std::vector<uint8_t> file = { 0x97, 0x4A, 0x42, 0x32, 0x0D, 0x0A, 0x1A, 0x0A, 0x01 };
    PushBE32(file, static_cast<uint32_t>(pages.size()));
    uint32_t seg = 0, sd_seg = 0;
    if (prep.emit_global_dict) {
        sd_seg = seg++;
        WriteSegHdr(file, sd_seg, 0, 0, static_cast<uint32_t>(prep.plan.dict_payload.size()));
        file.insert(file.end(), prep.plan.dict_payload.begin(), prep.plan.dict_payload.end());
    }
    for (size_t i = 0; i < pages.size(); ++i) {
        const uint32_t page = static_cast<uint32_t>(i + 1);
        WritePageInfo(file, seg, page, pages[i]);
        if (prep.emit_global_dict && prep.use_sym[i]) {
            WriteSegHdrRef1(file, seg++, 7, sd_seg, page, static_cast<uint32_t>(prep.plan.text_payloads[i].size()));
            file.insert(file.end(), prep.plan.text_payloads[i].begin(), prep.plan.text_payloads[i].end());
            WriteSegHdr(file, seg++, 39, page, static_cast<uint32_t>(prep.res_payloads[i].size()));
            file.insert(file.end(), prep.res_payloads[i].begin(), prep.res_payloads[i].end());
        } else if (prep.use_ht[i]) {
            const uint32_t pdd_seg = seg++;
            WriteSegHdr(file, pdd_seg, 16, page, static_cast<uint32_t>(prep.ht_plans[i].dict_payload.size()));
            file.insert(file.end(), prep.ht_plans[i].dict_payload.begin(), prep.ht_plans[i].dict_payload.end());
            WriteSegHdrRef1(file, seg++, 23, pdd_seg, page, static_cast<uint32_t>(prep.ht_plans[i].region_payload.size()));
            file.insert(file.end(), prep.ht_plans[i].region_payload.begin(), prep.ht_plans[i].region_payload.end());
        } else {
            WriteSegHdr(file, seg++, 39, page, static_cast<uint32_t>(prep.gen_payloads[i].size()));
            file.insert(file.end(), prep.gen_payloads[i].begin(), prep.gen_payloads[i].end());
        }
        WriteSegHdr(file, seg++, 49, page, 0);
    }
    WriteSegHdr(file, seg++, 51, 0, 0);
    return file;
}

PdfEncodedDocument EncodePdfDocumentView(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts) {
    PdfEncodedDocument pdf;
    auto prep = PrepareDocument(pages, opts);
    if (!prep.ok) return pdf;
    if (prep.emit_global_dict) {
        WriteSegHdr(pdf.globals, 0, 0, 0, static_cast<uint32_t>(prep.plan.dict_payload.size()));
        pdf.globals.insert(pdf.globals.end(), prep.plan.dict_payload.begin(), prep.plan.dict_payload.end());
    }
    pdf.pages.resize(pages.size());
    for (size_t i = 0; i < pages.size(); ++i) {
        auto& ps = pdf.pages[i];
        uint32_t seg = 1;
        WritePageInfo(ps, seg, 1, pages[i]);
        if (prep.emit_global_dict && prep.use_sym[i]) {
            WriteSegHdrRef1(ps, seg++, 7, 0, 1, static_cast<uint32_t>(prep.plan.text_payloads[i].size()));
            ps.insert(ps.end(), prep.plan.text_payloads[i].begin(), prep.plan.text_payloads[i].end());
            WriteSegHdr(ps, seg++, 39, 1, static_cast<uint32_t>(prep.res_payloads[i].size()));
            ps.insert(ps.end(), prep.res_payloads[i].begin(), prep.res_payloads[i].end());
        } else if (prep.use_ht[i]) {
            const uint32_t pdd_seg = seg++;
            WriteSegHdr(ps, pdd_seg, 16, 1, static_cast<uint32_t>(prep.ht_plans[i].dict_payload.size()));
            ps.insert(ps.end(), prep.ht_plans[i].dict_payload.begin(), prep.ht_plans[i].dict_payload.end());
            WriteSegHdrRef1(ps, seg++, 23, pdd_seg, 1, static_cast<uint32_t>(prep.ht_plans[i].region_payload.size()));
            ps.insert(ps.end(), prep.ht_plans[i].region_payload.begin(), prep.ht_plans[i].region_payload.end());
        } else {
            WriteSegHdr(ps, seg++, 39, 1, static_cast<uint32_t>(prep.gen_payloads[i].size()));
            ps.insert(ps.end(), prep.gen_payloads[i].begin(), prep.gen_payloads[i].end());
        }
        WriteSegHdr(ps, seg++, 49, 1, 0);
    }
    return pdf;
}

PdfEncodedDocument EncodePdfDocument(std::span<const Bitmap1bpp> pages, LosslessEncodeOptions opts) {
    std::vector<Bitmap1bppView> views; views.reserve(pages.size());
    for (const auto& p : pages) views.push_back({ p.pixels.data(), p.pixels.size(), p.width, p.height, p.stride, p.dpi_x, p.dpi_y });
    return EncodePdfDocumentView(views, opts);
}
std::vector<uint8_t> EncodeLosslessDocument(std::span<const Bitmap1bpp> pages, LosslessEncodeOptions opts) {
    std::vector<Bitmap1bppView> views; views.reserve(pages.size());
    for (const auto& p : pages) views.push_back({ p.pixels.data(), p.pixels.size(), p.width, p.height, p.stride, p.dpi_x, p.dpi_y });
    return EncodeLosslessDocumentView(views, opts);
}

std::vector<uint8_t> EncodeLosslessPage(const Bitmap1bpp& b, LosslessEncodeOptions opts) {
    Bitmap1bppView v{ b.pixels.data(), b.pixels.size(), b.width, b.height, b.stride, b.dpi_x, b.dpi_y };
    return EncodeLosslessDocumentView(std::span<const Bitmap1bppView>(&v, 1), opts);
}

} // namespace jbig2


