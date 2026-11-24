#include "jbig2_mq_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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

void WritePageInfo(std::vector<uint8_t>& file, uint32_t& seg, uint32_t page, const Bitmap1bppView& bmp) {
    WriteSegHdr(file, seg++, 48, page, 19);
    PushBE32(file, bmp.width); PushBE32(file, bmp.height);
    PushBE32(file, bmp.dpi_x > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_x / 0.0254)) : 0);
    PushBE32(file, bmp.dpi_y > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_y / 0.0254)) : 0);
    file.push_back(0x01); file.push_back(0x00); file.push_back(0x00);
}

} // namespace

std::vector<uint8_t> EncodeLosslessDocumentView(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts) {
    if (pages.empty()) return {};
    for (const auto& p : pages) {
        if (!p.pixels || p.width == 0 || p.height == 0 || p.stride < (p.width + 7) / 8 ||
            p.pixel_bytes < static_cast<size_t>(p.stride) * p.height) return {};
    }

    std::vector<uint8_t> file = { 0x97, 0x4A, 0x42, 0x32, 0x0D, 0x0A, 0x1A, 0x0A, 0x01 };
    PushBE32(file, static_cast<uint32_t>(pages.size()));
    std::vector<uint8_t> shared_ctx(65536, 0);
    uint32_t seg = 0;

    if (!opts.symbol_coding) {
        for (size_t i = 0; i < pages.size(); ++i) {
            const uint32_t page = static_cast<uint32_t>(i + 1);
            WritePageInfo(file, seg, page, pages[i]);
            const auto gen = EncodeGenericRegionPayload(pages[i], opts.adaptive_templates, shared_ctx.data());
            WriteSegHdr(file, seg++, 39, page, static_cast<uint32_t>(gen.size()));
            file.insert(file.end(), gen.begin(), gen.end());
            WriteSegHdr(file, seg++, 49, page, 0);
        }
        WriteSegHdr(file, seg++, 51, 0, 0);
        return file;
    }

    // Symbol-Coding mit Multi-Page Global-Dict & garantiertem Per-Page-Auto-Fallback (<= 0 %)
    auto plan = detail::BuildDocumentSymbolPlan(pages);
    std::vector<std::vector<uint8_t>> gen_payloads(pages.size());
    std::vector<std::vector<uint8_t>> res_payloads(pages.size());
    std::vector<bool> use_sym(pages.size(), false);
    int64_t total_saved = 0;

    for (size_t i = 0; i < pages.size(); ++i) {
        gen_payloads[i] = EncodeGenericRegionPayload(pages[i], opts.adaptive_templates, shared_ctx.data());
        if (plan.valid && plan.page_has_text[i]) {
            Bitmap1bppView res_view = pages[i];
            res_view.pixels = plan.residuals[i].data();
            res_view.pixel_bytes = plan.residuals[i].size();
            res_payloads[i] = EncodeGenericRegionPayload(res_view, opts.adaptive_templates, shared_ctx.data());
            const size_t sym_page_cost = plan.text_payloads[i].size() + 12u + res_payloads[i].size();
            if (sym_page_cost < gen_payloads[i].size()) {
                use_sym[i] = true;
                total_saved += static_cast<int64_t>(gen_payloads[i].size() - sym_page_cost);
            }
        }
    }

    const bool emit_global_dict = plan.valid && total_saved > static_cast<int64_t>(plan.dict_payload.size() + 12u);
    uint32_t sd_seg = 0;
    if (emit_global_dict) {
        sd_seg = seg++;
        WriteSegHdr(file, sd_seg, 0, 0, static_cast<uint32_t>(plan.dict_payload.size()));
        file.insert(file.end(), plan.dict_payload.begin(), plan.dict_payload.end());
    }

    for (size_t i = 0; i < pages.size(); ++i) {
        const uint32_t page = static_cast<uint32_t>(i + 1);
        WritePageInfo(file, seg, page, pages[i]);
        if (emit_global_dict && use_sym[i]) {
            WriteSegHdrRef1(file, seg++, 7, sd_seg, page, static_cast<uint32_t>(plan.text_payloads[i].size()));
            file.insert(file.end(), plan.text_payloads[i].begin(), plan.text_payloads[i].end());
            WriteSegHdr(file, seg++, 39, page, static_cast<uint32_t>(res_payloads[i].size()));
            file.insert(file.end(), res_payloads[i].begin(), res_payloads[i].end());
        } else {
            WriteSegHdr(file, seg++, 39, page, static_cast<uint32_t>(gen_payloads[i].size()));
            file.insert(file.end(), gen_payloads[i].begin(), gen_payloads[i].end());
        }
        WriteSegHdr(file, seg++, 49, page, 0);
    }

    WriteSegHdr(file, seg++, 51, 0, 0);
    return file;
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
