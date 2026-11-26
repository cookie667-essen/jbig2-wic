#pragma once
#include "jbig2_core.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#if defined(_MSC_VER)
#define JBIG2_NOINLINE __declspec(noinline)
#define JBIG2_FORCEINLINE __forceinline
#else
#define JBIG2_NOINLINE __attribute__((noinline))
#define JBIG2_FORCEINLINE inline __attribute__((always_inline))
#endif

namespace jbig2::detail {

struct MqState { uint16_t qe; uint8_t nmps; uint8_t nlps; uint8_t sw; };
inline constexpr std::array<MqState, 47> kMqTable = {{
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

class MqEncoder {
    uint32_t a_ = 0x8000, c_ = 0;
    int ct_ = 12, b_ = -1;
    std::vector<uint8_t> out_;

    JBIG2_NOINLINE void ByteOut() {
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

    JBIG2_FORCEINLINE void Renorm() {
        int s = std::countl_zero(a_) - 16;
        if (s < ct_) { a_ <<= s; c_ <<= s; ct_ -= s; return; }
        do { a_ <<= ct_; c_ <<= ct_; s -= ct_; ByteOut(); } while (s >= ct_);
        if (s > 0) { a_ <<= s; c_ <<= s; ct_ -= s; }
    }

public:
    explicit MqEncoder(size_t reserve_bytes) { out_.reserve(reserve_bytes); }

    JBIG2_FORCEINLINE bool TryEncode8Same(uint8_t* ctx, uint16_t cx, int d) {
        const uint8_t entry = ctx[cx];
        if ((entry >> 7) != d) return false;
        const uint32_t qe8 = static_cast<uint32_t>(kMqTable[entry & 0x7Fu].qe) << 3;
        if (a_ < 0x8000u + qe8) return false;
        a_ -= qe8; c_ += qe8;
        return true;
    }

    JBIG2_FORCEINLINE void EncodeBit(uint8_t* ctx, uint32_t cx, int d) {
        const uint8_t entry = ctx[cx];
        int mps = entry >> 7;
        const MqState& s = kMqTable[entry & 0x7Fu];
        a_ -= s.qe;
        if (d == mps) {
            if ((a_ & 0x8000u) == 0) {
                if (a_ < s.qe) a_ = s.qe; else c_ += s.qe;
                ctx[cx] = static_cast<uint8_t>((mps << 7) | s.nmps);
                Renorm();
            } else {
                c_ += s.qe;
            }
        } else {
            if (a_ < s.qe) c_ += s.qe; else a_ = s.qe;
            if (s.sw) mps ^= 1;
            ctx[cx] = static_cast<uint8_t>((mps << 7) | s.nlps);
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

// ITU-T T.88 Annex A.2 Arithmetic Integer Encoder (512 Kontexte)
struct ArithIntCtx {
    std::array<uint8_t, 512> ctx{};
    void EncodeBitPrev(MqEncoder& mq, uint32_t& prev, int d) {
        mq.EncodeBit(ctx.data(), prev, d);
        prev = (prev < 256u) ? ((prev << 1) | static_cast<uint32_t>(d))
                             : ((((prev << 1) | static_cast<uint32_t>(d)) & 511u) | 256u);
    }
    void Encode(MqEncoder& mq, int32_t val) {
        uint32_t prev = 1;
        const int s = (val < 0) ? 1 : 0;
        const uint32_t v = (val < 0) ? static_cast<uint32_t>(-static_cast<int64_t>(val)) : static_cast<uint32_t>(val);
        EncodeBitPrev(mq, prev, s);
        uint32_t bits = 0, off = 0;
        if (v < 4u)         { EncodeBitPrev(mq, prev, 0); bits = 2; off = 0; }
        else if (v < 20u)   { EncodeBitPrev(mq, prev, 1); EncodeBitPrev(mq, prev, 0); bits = 4; off = 4; }
        else if (v < 84u)   { EncodeBitPrev(mq, prev, 1); EncodeBitPrev(mq, prev, 1); EncodeBitPrev(mq, prev, 0); bits = 6; off = 20; }
        else if (v < 340u)  { for (int i = 0; i < 3; ++i) EncodeBitPrev(mq, prev, 1); EncodeBitPrev(mq, prev, 0); bits = 8; off = 84; }
        else if (v < 4436u) { for (int i = 0; i < 4; ++i) EncodeBitPrev(mq, prev, 1); EncodeBitPrev(mq, prev, 0); bits = 12; off = 340; }
        else                { for (int i = 0; i < 5; ++i) EncodeBitPrev(mq, prev, 1); bits = 32; off = 4436; }
        const uint32_t rem = v - off;
        for (int i = static_cast<int>(bits) - 1; i >= 0; --i)
            EncodeBitPrev(mq, prev, static_cast<int>((rem >> i) & 1u));
    }
    void EncodeOOB(MqEncoder& mq) {
        uint32_t prev = 1;
        EncodeBitPrev(mq, prev, 1);
        EncodeBitPrev(mq, prev, 0);
        EncodeBitPrev(mq, prev, 0);
        EncodeBitPrev(mq, prev, 0);
    }
};

// ITU-T T.88 Annex A.3 IAID Encoder
struct ArithIaidCtx {
    uint32_t codelen = 1;
    std::vector<uint8_t> ctx;
    explicit ArithIaidCtx(uint32_t num_syms) {
        codelen = 0;
        while ((1u << codelen) < num_syms) ++codelen;
        if (codelen == 0) codelen = 1;
        ctx.assign(size_t{1} << codelen, 0);
    }
    void Encode(MqEncoder& mq, uint32_t id) {
        uint32_t prev = 1;
        for (int i = static_cast<int>(codelen) - 1; i >= 0; --i) {
            const int d = static_cast<int>((id >> i) & 1u);
            mq.EncodeBit(ctx.data(), prev, d);
            prev = (prev << 1) | static_cast<uint32_t>(d);
        }
    }
};

inline void PushBE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
inline void PushBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8)  & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}
inline void WriteSegHdr(std::vector<uint8_t>& dst, uint32_t seg, uint8_t type, uint32_t page, uint32_t len) {
    PushBE32(dst, seg);
    const bool p4 = (page > 255);
    dst.push_back(static_cast<uint8_t>((type & 0x3Fu) | (p4 ? 0x40u : 0x00u)));
    dst.push_back(0x00);
    if (p4) PushBE32(dst, page); else dst.push_back(static_cast<uint8_t>(page));
    PushBE32(dst, len);
}
inline void WriteSegHdrRef1(std::vector<uint8_t>& dst, uint32_t seg, uint8_t type, uint32_t ref_seg, uint32_t page, uint32_t len) {
    PushBE32(dst, seg);
    const bool p4 = (page > 255);
    dst.push_back(static_cast<uint8_t>((type & 0x3Fu) | (p4 ? 0x40u : 0x00u)));
    dst.push_back(0x20);
    if (seg <= 256) dst.push_back(static_cast<uint8_t>(ref_seg));
    else if (seg <= 65536) PushBE16(dst, static_cast<uint16_t>(ref_seg));
    else PushBE32(dst, ref_seg);
    if (p4) PushBE32(dst, page); else dst.push_back(static_cast<uint8_t>(page));
    PushBE32(dst, len);
}

struct DocumentSymbolPlan {
    bool valid = false;
    std::vector<uint8_t> dict_payload;              // Segment 0 Payload (global, Page 0)
    std::vector<std::vector<uint8_t>> text_payloads; // Segment 7 Payloads pro Seite
    std::vector<std::vector<uint8_t>> residuals;     // Restbild-Bitmaps pro Seite
    std::vector<bool> page_has_text;
};

DocumentSymbolPlan BuildDocumentSymbolPlan(std::span<const Bitmap1bppView> pages, bool lossy_safe = false);


struct PageHalftonePlan {
    bool valid = false;
    std::vector<uint8_t> dict_payload;
    std::vector<uint8_t> region_payload;
};
PageHalftonePlan BuildPageHalftonePlan(const Bitmap1bppView& page);

} // namespace jbig2::detail
