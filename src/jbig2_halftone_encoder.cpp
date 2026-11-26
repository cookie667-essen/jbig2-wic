#include "jbig2_mq_internal.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <vector>

namespace jbig2::detail {
namespace {

inline int GetBit(const uint8_t* px, uint32_t w, uint32_t h, uint32_t stride,
                  int x, int y, bool invert) {
    if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h)) return 0;
    const int b = (px[static_cast<size_t>(y) * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
    return invert ? (b ^ 1) : b;
}

JBIG2_FORCEINLINE uint32_t RowBit(const uint8_t* row, int w, int x) {
    if (!row || x < 0 || x >= w) return 0u;
    return static_cast<uint32_t>((row[x >> 3] >> (7 - (x & 7))) & 1u);
}

void EncodeT0Bitmap(MqEncoder& mq, uint8_t* ctx, const uint8_t* px,
                    uint32_t w, uint32_t h, uint32_t stride,
                    int at0_dx, int at0_dy) {
    const int iw = static_cast<int>(w);
    const int ih = static_cast<int>(h);
    for (int y = 0; y < ih; ++y) {
        const uint8_t* r2 = (y >= 2) ? (px + static_cast<size_t>(y - 2) * stride) : nullptr;
        const uint8_t* r1 = (y >= 1) ? (px + static_cast<size_t>(y - 1) * stride) : nullptr;
        const uint8_t* r0 = px + static_cast<size_t>(y) * stride;
        const int ay = y + at0_dy;
        const uint8_t* rat = (ay >= 0 && ay < ih) ? (px + static_cast<size_t>(ay) * stride) : nullptr;
        for (int x = 0; x < iw; ++x) {
            uint32_t cx = 0;
            cx = (cx << 1) | RowBit(r2, iw, x - 2);
            cx = (cx << 1) | RowBit(r2, iw, x - 1);
            cx = (cx << 1) | RowBit(r2, iw, x);
            cx = (cx << 1) | RowBit(r2, iw, x + 1);
            cx = (cx << 1) | RowBit(r2, iw, x + 2);
            cx = (cx << 1) | RowBit(r1, iw, x - 3);
            cx = (cx << 1) | RowBit(r1, iw, x - 2);
            cx = (cx << 1) | RowBit(r1, iw, x - 1);
            cx = (cx << 1) | RowBit(r1, iw, x);
            cx = (cx << 1) | RowBit(r1, iw, x + 1);
            cx = (cx << 1) | RowBit(r1, iw, x + 2);
            cx = (cx << 1) | RowBit(rat, iw, x + at0_dx);
            cx = (cx << 1) | RowBit(r0, iw, x - 4);
            cx = (cx << 1) | RowBit(r0, iw, x - 3);
            cx = (cx << 1) | RowBit(r0, iw, x - 2);
            cx = (cx << 1) | RowBit(r0, iw, x - 1);
            mq.EncodeBit(ctx, cx, static_cast<int>(RowBit(r0, iw, x)));
        }
    }
}

} // namespace

PageHalftonePlan BuildPageHalftonePlan(const Bitmap1bppView& page) {
    PageHalftonePlan out;
    if (!page.pixels || page.width < 16 || page.height < 16) return out;
    constexpr uint32_t kP = 4;
    constexpr size_t kMaxPatterns = 64; // <= 6 Bitebenen (darueber ist Generic T0 fast immer kleiner)
    const uint32_t gw = (page.width + kP - 1) / kP;
    const uint32_t gh = (page.height + kP - 1) / kP;
    const size_t ntiles = static_cast<size_t>(gw) * gh;
    if (ntiles < 64) return out;

    std::vector<uint16_t> tile_pats(ntiles);
    std::array<uint64_t, 1024> seen{}; // 8-KB-Bitset (passt komplett in L1-Cache)
    std::vector<uint16_t> uniq;
    uniq.reserve(kMaxPatterns);

    const uint32_t full_gw = page.width / kP;
    const uint32_t full_gh = page.height / kP;

    for (uint32_t gy = 0; gy < gh; ++gy) {
        const uint32_t by = gy * kP;
        const bool full_y = (gy < full_gh);
        const uint8_t* p0 = full_y ? (page.pixels + static_cast<size_t>(by + 0) * page.stride) : nullptr;
        const uint8_t* p1 = full_y ? (page.pixels + static_cast<size_t>(by + 1) * page.stride) : nullptr;
        const uint8_t* p2 = full_y ? (page.pixels + static_cast<size_t>(by + 2) * page.stride) : nullptr;
        const uint8_t* p3 = full_y ? (page.pixels + static_cast<size_t>(by + 3) * page.stride) : nullptr;

        for (uint32_t gx = 0; gx < gw; ++gx) {
            uint16_t pat = 0;
            if (full_y && gx < full_gw) {
                const uint32_t byte_idx = gx >> 1;
                const uint32_t shift = (gx & 1u) ? 0u : 4u;
                pat = static_cast<uint16_t>(
                    ((((p0[byte_idx] >> shift) ^ 0x0Fu) & 0x0Fu) << 12) |
                    ((((p1[byte_idx] >> shift) ^ 0x0Fu) & 0x0Fu) << 8)  |
                    ((((p2[byte_idx] >> shift) ^ 0x0Fu) & 0x0Fu) << 4)  |
                    ((((p3[byte_idx] >> shift) ^ 0x0Fu) & 0x0Fu)));
            } else {
                const uint32_t bx = gx * kP;
                for (uint32_t dy = 0; dy < kP; ++dy) {
                    for (uint32_t dx = 0; dx < kP; ++dx) {
                        const int b = GetBit(page.pixels, page.width, page.height, page.stride,
                                             static_cast<int>(bx + dx), static_cast<int>(by + dy), true);
                        pat = static_cast<uint16_t>((pat << 1) | b);
                    }
                }
            }
            tile_pats[static_cast<size_t>(gy) * gw + gx] = pat;
            const uint64_t mask = 1ULL << (pat & 63u);
            if ((seen[pat >> 6] & mask) == 0) {
                seen[pat >> 6] |= mask;
                uniq.push_back(pat);
                if (uniq.size() > kMaxPatterns) return out;
            }
        }
    }
    if (uniq.size() < 2) return out;

    // Sortiere nach Schwarzanteil (glatte Gray-Code-Ebenen)
    std::sort(uniq.begin(), uniq.end(), [](uint16_t a, uint16_t b) {
        const int pa = std::popcount(a), pb = std::popcount(b);
        return (pa != pb) ? (pa < pb) : (a < b);
    });
    std::vector<uint8_t> pat_to_idx(65536, 0);
    for (size_t i = 0; i < uniq.size(); ++i) pat_to_idx[uniq[i]] = static_cast<uint8_t>(i);

    // 1. Pattern Dictionary (Segment 16, HDTEMPLATE=0, AT0=(-4,0))
    const uint32_t num_pats = static_cast<uint32_t>(uniq.size());
    const uint32_t dw = num_pats * kP;
    const uint32_t dstride = (dw + 7) / 8;
    std::vector<uint8_t> dbmp(static_cast<size_t>(dstride) * kP, 0);
    for (uint32_t i = 0; i < num_pats; ++i) {
        const uint16_t pat = uniq[i];
        for (uint32_t dy = 0; dy < kP; ++dy) {
            for (uint32_t dx = 0; dx < kP; ++dx) {
                if ((pat >> (15 - (dy * kP + dx))) & 1u) {
                    const uint32_t x = i * kP + dx;
                    dbmp[static_cast<size_t>(dy) * dstride + (x >> 3)] |=
                        static_cast<uint8_t>(1u << (7 - (x & 7)));
                }
            }
        }
    }
    out.dict_payload.reserve(64 + dbmp.size());
    out.dict_payload.push_back(0x00); // HDMMR=0, HDTEMPLATE=0
    out.dict_payload.push_back(static_cast<uint8_t>(kP));
    out.dict_payload.push_back(static_cast<uint8_t>(kP));
    PushBE32(out.dict_payload, num_pats - 1u);
    {
        std::vector<uint8_t> ctx(65536, 0);
        MqEncoder mq(256);
        EncodeT0Bitmap(mq, ctx.data(), dbmp.data(), dw, kP, dstride, -static_cast<int>(kP), 0);
        auto mqd = mq.Finish();
        out.dict_payload.insert(out.dict_payload.end(), mqd.begin(), mqd.end());
    }

    // 2. Halftone Region (Segment 23, HTEMPLATE=0, Gray-Code-Ebenen MSB->LSB)
    uint32_t bpp = 1;
    while ((1u << bpp) < num_pats) ++bpp;
    const uint32_t gstride = (gw + 7) / 8;
    std::vector<uint8_t> plane(static_cast<size_t>(gstride) * gh, 0);

    out.region_payload.reserve(64 + ntiles / 4);
    PushBE32(out.region_payload, page.width);
    PushBE32(out.region_payload, page.height);
    PushBE32(out.region_payload, 0);
    PushBE32(out.region_payload, 0);
    out.region_payload.push_back(0x04); // REPLACE
    out.region_payload.push_back(0x00); // HMMR=0, HTEMPLATE=0, SKIP=0, HCOMBOP=OR, HDEFPIXEL=0
    PushBE32(out.region_payload, gw);
    PushBE32(out.region_payload, gh);
    PushBE32(out.region_payload, 0);
    PushBE32(out.region_payload, 0);
    PushBE16(out.region_payload, static_cast<uint16_t>(kP * 256u));
    PushBE16(out.region_payload, 0);

    std::vector<uint8_t> ctx(65536, 0);
    MqEncoder mq(ntiles / 4 + 64);
    for (int b = static_cast<int>(bpp) - 1; b >= 0; --b) {
        std::fill(plane.begin(), plane.end(), 0);
        for (uint32_t gy = 0; gy < gh; ++gy) {
            const size_t row_off = static_cast<size_t>(gy) * gw;
            uint8_t* prow = plane.data() + static_cast<size_t>(gy) * gstride;
            for (uint32_t gx = 0; gx < gw; ++gx) {
                const uint32_t idx = pat_to_idx[tile_pats[row_off + gx]];
                const uint32_t gray = idx ^ (idx >> 1);
                if ((gray >> b) & 1u) {
                    prow[gx >> 3] |= static_cast<uint8_t>(1u << (7 - (gx & 7)));
                }
            }
        }
        EncodeT0Bitmap(mq, ctx.data(), plane.data(), gw, gh, gstride, 3, -1);
    }
    auto mqh = mq.Finish();
    out.region_payload.insert(out.region_payload.end(), mqh.begin(), mqh.end());
    out.valid = true;
    return out;
}

} // namespace jbig2::detail
