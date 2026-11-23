#include "jbig2_core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jbig2 {
namespace {

struct MqState { uint16_t qe; uint8_t nmps; uint8_t nlps; uint8_t sw; };
constexpr std::array<MqState, 47> kMqTable = {{
    {0x5601,  1,  1, 1}, {0x3401,  2,  6, 0}, {0x1801,  3,  9, 0}, {0x0AC1,  4, 12, 0},
    {0x0521,  5, 29, 0}, {0x0221, 38, 33, 0}, {0x5601,  7,  6, 1}, {0x5401,  8, 14, 0},
    {0x4801,  9, 14, 0}, {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
    {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0},
    {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
    {0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
    {0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
    {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0},
    {0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02A1, 36, 33, 0},
    {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
    {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0},
    {0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0}
}};

struct AtCoord { int8_t dx; int8_t dy; };
constexpr std::array<AtCoord, 4> kDefaultAt = {{{3, -1}, {-3, -1}, {2, -2}, {-2, -2}}};

class MqEncoder {
    uint32_t a_ = 0x8000, c_ = 0;
    int ct_ = 12, b_ = -1;
    std::vector<uint8_t> out_;
    uint8_t* ctx_;

    void ByteOut() {
        if (b_ == 0xFF) {
            out_.push_back(0xFF); b_ = static_cast<int>(c_ >> 20); c_ &= 0xFFFFFu; ct_ = 7;
        } else if (c_ < 0x8000000u) {
            if (b_ >= 0) out_.push_back(static_cast<uint8_t>(b_));
            b_ = static_cast<int>(c_ >> 19); c_ &= 0x7FFFFu; ct_ = 8;
        } else {
            b_++;
            if (b_ == 0xFF) {
                c_ &= 0x7FFFFFFu; out_.push_back(0xFF); b_ = static_cast<int>(c_ >> 20); c_ &= 0xFFFFFu; ct_ = 7;
            } else {
                out_.push_back(static_cast<uint8_t>(b_)); b_ = static_cast<int>(c_ >> 19); c_ &= 0x7FFFFu; ct_ = 8;
            }
        }
    }
    inline void Renorm() {
        do { a_ <<= 1; c_ <<= 1; if (--ct_ == 0) ByteOut(); } while ((a_ & 0x8000u) == 0);
    }

public:
    MqEncoder(uint8_t* shared_ctx, size_t reserve_bytes) : ctx_(shared_ctx) {
        std::memset(ctx_, 0, 65536);
        out_.reserve(reserve_bytes);
    }
    inline void EncodeBit(uint16_t cx, int d) {
        uint8_t entry = ctx_[cx];
        int mps = entry >> 7;
        const MqState& s = kMqTable[entry & 0x7Fu];
        a_ -= s.qe;
        if (d == mps) {
            if ((a_ & 0x8000u) == 0) {
                if (a_ < s.qe) a_ = s.qe; else c_ += s.qe;
                ctx_[cx] = static_cast<uint8_t>((mps << 7) | s.nmps);
                Renorm();
            } else { c_ += s.qe; }
        } else {
            if (a_ < s.qe) c_ += s.qe; else a_ = s.qe;
            if (s.sw) mps ^= 1;
            ctx_[cx] = static_cast<uint8_t>((mps << 7) | s.nlps);
            Renorm();
        }
    }
    std::vector<uint8_t> Finish() {
        uint32_t tempc = c_ + a_;
        c_ |= 0xFFFFu;
        if (c_ >= tempc) c_ -= 0x8000u;
        c_ <<= ct_; ByteOut(); c_ <<= ct_; ByteOut();
        if (b_ >= 0 && b_ != 0xFF) out_.push_back(static_cast<uint8_t>(b_));
        out_.push_back(0xFF); out_.push_back(0xAC);
        return std::move(out_);
    }
};

void PushBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8)  & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
void WriteSegHdr(std::vector<uint8_t>& dst, uint32_t seg, uint8_t type, uint32_t page, uint32_t len) {
    PushBE32(dst, seg);
    const bool p4 = (page > 255);
    dst.push_back(static_cast<uint8_t>((type & 0x3Fu) | (p4 ? 0x40u : 0x00u)));
    dst.push_back(0x00);
    if (p4) PushBE32(dst, page); else dst.push_back(static_cast<uint8_t>(page));
    PushBE32(dst, len);
}
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

void AppendPage(std::vector<uint8_t>& file, uint32_t& seg, uint32_t page,
                const Bitmap1bppView& bmp, LosslessEncodeOptions opts, uint8_t* shared_ctx) {
    const uint32_t w = bmp.width, h = bmp.height;
    std::vector<uint8_t> blank(bmp.stride, 0xFF);
    const auto at = opts.adaptive_templates ? TuneAT(bmp) : kDefaultAt;
    const bool def_at = std::equal(at.begin(), at.end(), kDefaultAt.begin(),
                                   [](AtCoord a, AtCoord b) { return a.dx == b.dx && a.dy == b.dy; });

    MqEncoder mq(shared_ctx, std::max<size_t>(1024, (static_cast<size_t>(w) * h) / 32));
    int ltp = 0;
    const uint32_t safe_bytes = (w > 4) ? ((w - 4) >> 3) : 0;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* r0 = bmp.pixels + static_cast<size_t>(y) * bmp.stride;
        const uint8_t* r1 = (y >= 1) ? (bmp.pixels + static_cast<size_t>(y - 1) * bmp.stride) : blank.data();
        const uint8_t* r2 = (y >= 2) ? (bmp.pixels + static_cast<size_t>(y - 2) * bmp.stride) : blank.data();

        const int same = RowsEqual(r0, r1, w) ? 1 : 0;
        mq.EncodeBit(0x9B25, same != ltp);
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
                for (int k = 0; k < 8; ++k) {
                    const int d = static_cast<int>((w0 >> (7 - k)) & 1u);
                    mq.EncodeBit(static_cast<uint16_t>(((c2 & 0x1Fu) << 11) | ((c1 & 0x7Fu) << 4) | (c0 & 0x0Fu)), d);
                    c2 = ((c2 << 1) | ((w2 >> (12 - k)) & 1u)) & 0x1Fu;
                    c1 = ((c1 << 1) | ((w1 >> (11 - k)) & 1u)) & 0x7Fu;
                    c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0x0Fu;
                }
            }
            for (uint32_t x = safe_bytes << 3; x < w; ++x) {
                const int d = GetBit(r0, x, w);
                mq.EncodeBit(static_cast<uint16_t>(((c2 & 0x1Fu) << 11) | ((c1 & 0x7Fu) << 4) | (c0 & 0x0Fu)), d);
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
                mq.EncodeBit(static_cast<uint16_t>((a3 << 15) | ((c2 & 7u) << 12) | (a2 << 11) |
                                                   (a1 << 10) | ((c1 & 0x1Fu) << 5) | (a0 << 4) | (c0 & 0xFu)), d);
                c2 = ((c2 << 1) | GetBit(r2, x + 2, w)) & 7u;
                c1 = ((c1 << 1) | GetBit(r1, x + 3, w)) & 0x1Fu;
                c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0xFu;
            }
        }
    }

    const auto mq_bytes = mq.Finish();
    WriteSegHdr(file, seg++, 48, page, 19);
    PushBE32(file, w); PushBE32(file, h);
    PushBE32(file, bmp.dpi_x > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_x / 0.0254)) : 0);
    PushBE32(file, bmp.dpi_y > 0 ? static_cast<uint32_t>(std::llround(bmp.dpi_y / 0.0254)) : 0);
    file.push_back(0x01); file.push_back(0x00); file.push_back(0x00);

    WriteSegHdr(file, seg++, 39, page, 26u + static_cast<uint32_t>(mq_bytes.size()));
    PushBE32(file, w); PushBE32(file, h); PushBE32(file, 0); PushBE32(file, 0);
    file.push_back(0x00); file.push_back(0x08);
    for (int i = 0; i < 4; ++i) { file.push_back(static_cast<uint8_t>(at[i].dx)); file.push_back(static_cast<uint8_t>(at[i].dy)); }
    file.insert(file.end(), mq_bytes.begin(), mq_bytes.end());
    WriteSegHdr(file, seg++, 49, page, 0);
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
    for (size_t i = 0; i < pages.size(); ++i) AppendPage(file, seg, static_cast<uint32_t>(i + 1), pages[i], opts, shared_ctx.data());
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
