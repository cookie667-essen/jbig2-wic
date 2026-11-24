#include "jbig2_mq_internal.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jbig2::detail {
namespace {

struct BBox { uint32_t x0 = 0xFFFFFFFFu, y0 = 0xFFFFFFFFu, x1 = 0, y1 = 0, count = 0; };

struct Glyph {
    uint32_t page_idx, x, y, w, h, black_cnt;
    uint64_t mask;
    std::array<uint64_t, 72> rows{}; // Bit x = Pixel x (1 = schwarz, 0 = weiß)
};

inline int32_t FloorDiv2(int32_t v) { return (v >= 0) ? (v / 2) : ((v - 1) / 2); }
inline uint32_t RowBit(uint64_t r, int x) {
    return (static_cast<unsigned>(x) < 64u) ? static_cast<uint32_t>((r >> x) & 1u) : 0u;
}

// Template-0 Generic Region ohne TPGDON (rollender Kontext über uint64_t-Zeilen)
void EncodeSymbolBitmapT0(MqEncoder& mq, uint8_t* gb_ctx, const Glyph& g) {
    for (uint32_t y = 0; y < g.h; ++y) {
        const uint64_t r0 = g.rows[y];
        const uint64_t r1 = (y >= 1) ? g.rows[y - 1] : 0u;
        const uint64_t r2 = (y >= 2) ? g.rows[y - 2] : 0u;

        uint32_t c2 = (RowBit(r2, 0) << 2) | (RowBit(r2, 1) << 1) | RowBit(r2, 2);
        uint32_t c1 = (RowBit(r1, 0) << 3) | (RowBit(r1, 1) << 2) | (RowBit(r1, 2) << 1) | RowBit(r1, 3);
        uint32_t c0 = 0;

        for (uint32_t x = 0; x < g.w; ++x) {
            const int d = static_cast<int>((r0 >> x) & 1u);
            mq.EncodeBit(gb_ctx, ((c2 & 0x1Fu) << 11) | ((c1 & 0x7Fu) << 4) | (c0 & 0x0Fu), d);
            c2 = ((c2 << 1) | RowBit(r2, static_cast<int>(x) + 3)) & 0x1Fu;
            c1 = ((c1 << 1) | RowBit(r1, static_cast<int>(x) + 4)) & 0x7Fu;
            c0 = ((c0 << 1) | static_cast<uint32_t>(d)) & 0x0Fu;
        }
    }
}

// ITU-T T.88 6.3.5.3 Figure 13: GRRD Template 1 (10-Bit Kontext, 0 AT-Bytes, 100 % verlustfrei)
void EncodeRefinementT1(MqEncoder& mq, uint8_t* gr_ctx, const Glyph& cur, const Glyph& ref,
                        int ref_dx, int ref_dy) {
    const int rh = static_cast<int>(ref.h);
    for (int y = 0; y < static_cast<int>(cur.h); ++y) {
        const uint64_t c0 = cur.rows[y];
        const uint64_t cp = (y > 0) ? cur.rows[y - 1] : 0u;
        const int ry = y - ref_dy;
        const uint64_t ru = (ry - 1 >= 0 && ry - 1 < rh) ? ref.rows[ry - 1] : 0u;
        const uint64_t rm = (ry     >= 0 && ry     < rh) ? ref.rows[ry]     : 0u;
        const uint64_t rd = (ry + 1 >= 0 && ry + 1 < rh) ? ref.rows[ry + 1] : 0u;

        for (int x = 0; x < static_cast<int>(cur.w); ++x) {
            const int rx = x - ref_dx;
            uint32_t cx = 0;
            cx |= RowBit(cp, x - 1) << 9;
            cx |= RowBit(cp, x)     << 8;
            cx |= RowBit(cp, x + 1) << 7;
            cx |= RowBit(c0, x - 1) << 6;
            cx |= RowBit(ru, rx)     << 5;
            cx |= RowBit(rm, rx - 1) << 4;
            cx |= RowBit(rm, rx)     << 3;
            cx |= RowBit(rm, rx + 1) << 2;
            cx |= RowBit(rd, rx)     << 1;
            cx |= RowBit(rd, rx + 1);
            mq.EncodeBit(gr_ctx, cx, static_cast<int>((c0 >> x) & 1u));
        }
    }
}

// Ultra-schneller 64-Bit-Zeilen-Hamming-Abstand inkl. ±1/±2 px Shift
inline uint32_t HammingShift(const Glyph& a, const Glyph& b, int ref_dx, int ref_dy, uint32_t limit) {
    uint32_t d = 0;
    const int bh = static_cast<int>(b.h);
    for (int y = 0; y < static_cast<int>(a.h); ++y) {
        const int ry = y - ref_dy;
        const uint64_t rb = (ry >= 0 && ry < bh) ? b.rows[ry] : 0u;
        const uint64_t bs = (ref_dx >= 0) ? ((rb << ref_dx) & a.mask) : ((rb >> -ref_dx) & a.mask);
        d += static_cast<uint32_t>(std::popcount(a.rows[y] ^ bs));
        if (d > limit) return d;
    }
    return d;
}

// 1-Pass-BBox 4-Connected-Component-Labeling mit 0xFF-Byte-Skipping & Puffer-Wiederverwendung
void ExtractGlyphsForPage(const Bitmap1bppView& bmp, uint32_t page_idx,
                          std::vector<uint32_t>& labels, std::vector<Glyph>& out) {
    const uint32_t w = bmp.width, h = bmp.height;
    const size_t total_px = static_cast<size_t>(w) * h;
    if (labels.size() < total_px) labels.resize(total_px);
    std::memset(labels.data(), 0, total_px * sizeof(uint32_t));

    std::vector<uint32_t> parent(1, 0);
    std::vector<BBox> boxes(1);
    parent.reserve(4096);
    boxes.reserve(4096);

    auto find = [&](uint32_t x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](uint32_t a, uint32_t b) {
        a = find(a); b = find(b);
        if (a != b) { if (a < b) parent[b] = a; else parent[a] = b; }
    };

    const uint32_t full_bytes = w >> 3;
    const uint32_t rem_bits = w & 7u;
    const uint8_t tail_mask = rem_bits ? static_cast<uint8_t>(0xFFu << (8u - rem_bits)) : 0u;

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t* row = labels.data() + static_cast<size_t>(y) * w;
        const uint32_t* up = (y > 0) ? (row - w) : nullptr;
        const uint8_t* prow = bmp.pixels + static_cast<size_t>(y) * bmp.stride;
        const uint32_t byte_cnt = full_bytes + (rem_bits ? 1u : 0u);

        for (uint32_t bx = 0; bx < byte_cnt; ++bx) {
            uint8_t inv = static_cast<uint8_t>(~prow[bx]);
            if (bx == full_bytes) inv &= tail_mask;
            if (inv == 0u) continue; // 8 weiße Pixel in 1 Takt überspringen

            while (inv) {
                const uint32_t bit = static_cast<uint32_t>(std::countl_zero(inv));
                inv &= static_cast<uint8_t>(~(0x80u >> bit));
                const uint32_t x = (bx << 3) + bit;
                const uint32_t l = (x > 0) ? row[x - 1] : 0;
                const uint32_t u = up ? up[x] : 0;

                uint32_t id;
                if (l && u) {
                    id = (l < u) ? l : u;
                    if (l != u) unite(l, u);
                } else if (l) {
                    id = l;
                } else if (u) {
                    id = u;
                } else {
                    id = static_cast<uint32_t>(parent.size());
                    parent.push_back(id);
                    boxes.push_back({x, y, x, y, 0});
                }
                row[x] = id;
                BBox& b = boxes[id];
                if (x < b.x0) b.x0 = x;
                if (x > b.x1) b.x1 = x;
                if (y < b.y0) b.y0 = y;
                if (y > b.y1) b.y1 = y;
                b.count++;
            }
        }
    }

    // Root-Kompression & O(Komponenten)-BBox-Merge (Pass 2 über das Gesamtbild entfällt!)
    for (uint32_t i = 1; i < parent.size(); ++i) {
        const uint32_t r = parent[parent[i]];
        parent[i] = r;
        if (r != i && boxes[i].count > 0) {
            BBox& dst = boxes[r];
            const BBox& src = boxes[i];
            if (src.x0 < dst.x0) dst.x0 = src.x0;
            if (src.x1 > dst.x1) dst.x1 = src.x1;
            if (src.y0 < dst.y0) dst.y0 = src.y0;
            if (src.y1 > dst.y1) dst.y1 = src.y1;
            dst.count += src.count;
            boxes[i].count = 0;
        }
    }

    // Nur überlebende Buchstaben-Kandidaten aus labels extrahieren
    for (uint32_t r = 1; r < boxes.size(); ++r) {
        const BBox& b = boxes[r];
        if (b.count < 22) continue;
        const uint32_t bw = b.x1 - b.x0 + 1;
        const uint32_t bh = b.y1 - b.y0 + 1;
        if (bw < 6 || bw > 64 || bh < 8 || bh > 72 || bw * bh < 72) continue;

        Glyph g{page_idx, b.x0, b.y0, bw, bh, b.count,
                (bw == 64u) ? ~uint64_t{0} : ((uint64_t{1} << bw) - 1u), {}};
        for (uint32_t yy = 0; yy < bh; ++yy) {
            const uint32_t* lrow = labels.data() + static_cast<size_t>(b.y0 + yy) * w + b.x0;
            uint64_t bits = 0;
            for (uint32_t xx = 0; xx < bw; ++xx) {
                const uint32_t lbl = lrow[xx];
                if (lbl && parent[lbl] == r) bits |= (uint64_t{1} << xx);
            }
            g.rows[yy] = bits;
        }
        out.push_back(g);
    }
}

} // namespace

DocumentSymbolPlan BuildDocumentSymbolPlan(std::span<const Bitmap1bppView> pages) {
    DocumentSymbolPlan plan;
    std::vector<Glyph> glyphs;
    glyphs.reserve(pages.size() * 1500);
    std::vector<uint32_t> labels;
    for (uint32_t p = 0; p < pages.size(); ++p) ExtractGlyphsForPage(pages[p], p, labels, glyphs);
    labels.clear();
    labels.shrink_to_fit();
    if (glyphs.size() < 12) return plan;

    struct Member { uint32_t glyph_idx; bool refined; int8_t rdx; int8_t rdy; };
    struct Group { uint32_t proto_idx; std::vector<Member> members; uint32_t exact_cnt = 1; };
    std::vector<Group> groups;
    groups.reserve(glyphs.size() / 2 + 16);

    std::vector<std::vector<uint32_t>> buckets(73 * 65);
    auto bidx = [](uint32_t w, uint32_t h) { return h * 65u + w; };
    constexpr std::array<std::pair<int8_t, int8_t>, 4> kExtraShifts = {{{-1,0}, {1,0}, {0,-1}, {0,1}}};

    for (uint32_t i = 0; i < glyphs.size(); ++i) {
        const Glyph& g = glyphs[i];
        const uint32_t max_diff = std::min<uint32_t>(14u, std::max<uint32_t>(2u, (g.w * g.h) / 16u));
        uint32_t best_gid = 0xFFFFFFFFu;
        uint32_t best_dist = max_diff + 1u;
        int8_t best_rdx = 0, best_rdy = 0;

        const uint32_t h_lo = (g.h > 8) ? g.h - 1 : g.h;
        const uint32_t h_hi = (g.h < 72) ? g.h + 1 : g.h;
        const uint32_t w_lo = (g.w > 6) ? g.w - 1 : g.w;
        const uint32_t w_hi = (g.w < 64) ? g.w + 1 : g.w;

        for (uint32_t bh = h_lo; bh <= h_hi && best_dist > 0; ++bh) {
            for (uint32_t bw = w_lo; bw <= w_hi && best_dist > 0; ++bw) {
                for (uint32_t gid : buckets[bidx(bw, bh)]) {
                    const Glyph& p = glyphs[groups[gid].proto_idx];
                    const uint32_t cnt_diff = (g.black_cnt > p.black_cnt) ? (g.black_cnt - p.black_cnt) : (p.black_cnt - g.black_cnt);
                    if (cnt_diff >= best_dist) continue;

                    const int base_dx = FloorDiv2(static_cast<int32_t>(g.w) - static_cast<int32_t>(p.w));
                    const int base_dy = FloorDiv2(static_cast<int32_t>(g.h) - static_cast<int32_t>(p.h));
                    const uint32_t d0 = HammingShift(g, p, base_dx, base_dy, std::max(best_dist - 1u, max_diff * 2u));
                    if (d0 < best_dist) {
                        best_dist = d0; best_gid = gid; best_rdx = 0; best_rdy = 0;
                        if (d0 == 0) break;
                    }
                    if (d0 <= max_diff * 2u && best_dist > 1u) {
                        for (auto [sdx, sdy] : kExtraShifts) {
                            const uint32_t d = HammingShift(g, p, base_dx + sdx, base_dy + sdy, best_dist - 1u);
                            if (d < best_dist) {
                                best_dist = d; best_gid = gid; best_rdx = sdx; best_rdy = sdy;
                                if (d == 0) break;
                            }
                        }
                    }
                }
            }
        }

        if (best_gid != 0xFFFFFFFFu) {
            const Glyph& p = glyphs[groups[best_gid].proto_idx];
            const bool is_exact = (best_dist == 0 && p.w == g.w && p.h == g.h && best_rdx == 0 && best_rdy == 0);
            groups[best_gid].members.push_back({i, !is_exact, best_rdx, best_rdy});
            if (is_exact) groups[best_gid].exact_cnt++;
        } else {
            const uint32_t gid = static_cast<uint32_t>(groups.size());
            groups.push_back({i, {{i, false, 0, 0}}, 1});
            buckets[bidx(g.w, g.h)].push_back(gid);
        }
    }

    std::vector<uint32_t> active_groups;
    uint32_t total_instances = 0;
    for (uint32_t gi = 0; gi < groups.size(); ++gi) {
        const auto& grp = groups[gi];
        if (grp.members.size() >= 3 || (grp.members.size() == 2 && grp.exact_cnt == 2)) {
            active_groups.push_back(gi);
            total_instances += static_cast<uint32_t>(grp.members.size());
        }
    }
    if (active_groups.size() < 2 || total_instances < 20) return plan;

    std::sort(active_groups.begin(), active_groups.end(), [&](uint32_t a, uint32_t b) {
        const Glyph& ga = glyphs[groups[a].proto_idx];
        const Glyph& gb = glyphs[groups[b].proto_idx];
        return (ga.h != gb.h) ? (ga.h < gb.h) : (ga.w < gb.w);
    });

    struct Instance { uint32_t x, y_bot, w, sym_id, glyph_idx; bool refined; int8_t rdx, rdy; };
    std::vector<std::vector<Instance>> page_insts(pages.size());
    std::vector<uint32_t> dict_proto_indices;
    dict_proto_indices.reserve(active_groups.size());

    for (uint32_t sym_id = 0; sym_id < active_groups.size(); ++sym_id) {
        const Group& grp = groups[active_groups[sym_id]];
        dict_proto_indices.push_back(grp.proto_idx);
        for (const Member& m : grp.members) {
            const Glyph& g = glyphs[m.glyph_idx];
            page_insts[g.page_idx].push_back({g.x, g.y + g.h - 1u, g.w, sym_id, m.glyph_idx, m.refined, m.rdx, m.rdy});
        }
    }

    // 1. Globales Symbol Dictionary (Segment 0 Payload)
    const uint32_t num_syms = static_cast<uint32_t>(dict_proto_indices.size());
    std::vector<uint8_t> gb_ctx(65536, 0);
    ArithIntCtx iadh, iadw, iaex;
    MqEncoder mq_sd(num_syms * 16 + 256);

    uint32_t hc_height = 0;
    size_t idx = 0;
    while (idx < num_syms) {
        const uint32_t h_cur = glyphs[dict_proto_indices[idx]].h;
        iadh.Encode(mq_sd, static_cast<int32_t>(h_cur - hc_height));
        hc_height = h_cur;
        uint32_t sym_w = 0;
        while (idx < num_syms && glyphs[dict_proto_indices[idx]].h == hc_height) {
            const Glyph& g = glyphs[dict_proto_indices[idx]];
            iadw.Encode(mq_sd, static_cast<int32_t>(g.w) - static_cast<int32_t>(sym_w));
            sym_w = g.w;
            EncodeSymbolBitmapT0(mq_sd, gb_ctx.data(), g);
            ++idx;
        }
        iadw.EncodeOOB(mq_sd);
    }
    iaex.Encode(mq_sd, 0);
    iaex.Encode(mq_sd, static_cast<int32_t>(num_syms));
    const auto sd_bytes = mq_sd.Finish();

    plan.dict_payload.reserve(18u + sd_bytes.size());
    PushBE16(plan.dict_payload, 0x0000);
    const int8_t sdat[8] = { 3, -1, -3, -1, 2, -2, -2, -2 };
    for (int8_t v : sdat) plan.dict_payload.push_back(static_cast<uint8_t>(v));
    PushBE32(plan.dict_payload, num_syms);
    PushBE32(plan.dict_payload, num_syms);
    plan.dict_payload.insert(plan.dict_payload.end(), sd_bytes.begin(), sd_bytes.end());

    // 2. Text Regions & Residual-Bitmaps pro Seite
    plan.text_payloads.resize(pages.size());
    plan.residuals.resize(pages.size());
    plan.page_has_text.assign(pages.size(), false);
    constexpr int32_t kStrips = 8;

    for (size_t p = 0; p < pages.size(); ++p) {
        auto& insts = page_insts[p];
        if (insts.size() < 8) continue;

        plan.page_has_text[p] = true;
        plan.residuals[p].assign(pages[p].pixels, pages[p].pixels + static_cast<size_t>(pages[p].stride) * pages[p].height);
        for (const Instance& inst : insts) {
            const Glyph& g = glyphs[inst.glyph_idx];
            for (uint32_t yy = 0; yy < g.h; ++yy) {
                uint8_t* dst = plan.residuals[p].data() + static_cast<size_t>(g.y + yy) * pages[p].stride;
                uint64_t bits = g.rows[yy];
                while (bits) {
                    const uint32_t xx = static_cast<uint32_t>(std::countr_zero(bits));
                    bits &= bits - 1u;
                    const uint32_t px = g.x + xx;
                    dst[px >> 3] |= static_cast<uint8_t>(1u << (7u - (px & 7u)));
                }
            }
        }

        std::sort(insts.begin(), insts.end(), [](const Instance& a, const Instance& b) {
            const uint32_t sa = a.y_bot / kStrips, sb = b.y_bot / kStrips;
            if (sa != sb) return sa < sb;
            return (a.x != b.x) ? (a.x < b.x) : (a.y_bot < b.y_bot);
        });

        ArithIntCtx iadt, iafs, iads, iait, iari, iardw, iardh, iardx, iardy;
        ArithIaidCtx iaid(num_syms);
        std::array<uint8_t, 1024> gr_ctx{};
        MqEncoder mq_tr(insts.size() * 8 + 256);

        auto emitInstance = [&](const Instance& inst, int32_t strip_y0) {
            iait.Encode(mq_tr, static_cast<int32_t>(inst.y_bot) - strip_y0);
            iaid.Encode(mq_tr, inst.sym_id);
            iari.Encode(mq_tr, inst.refined ? 1 : 0);
            if (inst.refined) {
                const Glyph& cur = glyphs[inst.glyph_idx];
                const Glyph& ref = glyphs[dict_proto_indices[inst.sym_id]];
                const int32_t dw = static_cast<int32_t>(cur.w) - static_cast<int32_t>(ref.w);
                const int32_t dh = static_cast<int32_t>(cur.h) - static_cast<int32_t>(ref.h);
                iardw.Encode(mq_tr, dw);
                iardh.Encode(mq_tr, dh);
                iardx.Encode(mq_tr, inst.rdx);
                iardy.Encode(mq_tr, inst.rdy);
                EncodeRefinementT1(mq_tr, gr_ctx.data(), cur, ref,
                                   FloorDiv2(dw) + inst.rdx, FloorDiv2(dh) + inst.rdy);
            }
        };

        iadt.Encode(mq_tr, 0);
        int32_t strip_idx = 0, firsts = 0;
        size_t ii = 0;
        while (ii < insts.size()) {
            const int32_t cur_strip = static_cast<int32_t>(insts[ii].y_bot / kStrips);
            iadt.Encode(mq_tr, cur_strip - strip_idx);
            strip_idx = cur_strip;
            const int32_t strip_y0 = cur_strip * kStrips;

            const Instance& first = insts[ii++];
            const int32_t dfs = static_cast<int32_t>(first.x) - firsts;
            iafs.Encode(mq_tr, dfs);
            firsts += dfs;
            int32_t curs = firsts;
            emitInstance(first, strip_y0);
            curs += static_cast<int32_t>(first.w) - 1;

            while (ii < insts.size() && static_cast<int32_t>(insts[ii].y_bot / kStrips) == cur_strip) {
                const Instance& inst = insts[ii++];
                iads.Encode(mq_tr, static_cast<int32_t>(inst.x) - curs);
                curs = static_cast<int32_t>(inst.x);
                emitInstance(inst, strip_y0);
                curs += static_cast<int32_t>(inst.w) - 1;
            }
            iads.EncodeOOB(mq_tr);
        }
        const auto tr_bytes = mq_tr.Finish();

        auto& tp = plan.text_payloads[p];
        tp.reserve(23u + tr_bytes.size());
        PushBE32(tp, pages[p].width); PushBE32(tp, pages[p].height);
        PushBE32(tp, 0); PushBE32(tp, 0);
        tp.push_back(0x00);    // CombOp = OR
        PushBE16(tp, 0x800E);  // SBRTEMPLATE=1, REFCORNER=BOTTOMLEFT(0), LOGSBSTRIPS=3(8), SBREFINE=1
        PushBE32(tp, static_cast<uint32_t>(insts.size()));
        tp.insert(tp.end(), tr_bytes.begin(), tr_bytes.end());
    }

    plan.valid = true;
    return plan;
}

} // namespace jbig2::detail
