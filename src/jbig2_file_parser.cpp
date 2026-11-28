#include "jbig2_file_parser.h"
#include <algorithm>
#include <array>

namespace jbig2::detail {

inline constexpr std::array<uint8_t, 8> kSig = {
    0x97, 0x4A, 0x42, 0x32, 0x0D, 0x0A, 0x1A, 0x0A
};

// Harte Sicherheitslimits gegen manipulierte/kaputte Dateien
inline constexpr uint32_t kMaxPages  = 10000;
inline constexpr uint32_t kMaxDim    = 65536;
inline constexpr uint64_t kMaxPixels = 256ull * 1024 * 1024; // 256 MP

static uint32_t ReadBE32(std::span<const uint8_t> s, size_t off) {
    return (uint32_t(s[off]) << 24) | (uint32_t(s[off + 1]) << 16) |
           (uint32_t(s[off + 2]) << 8) | uint32_t(s[off + 3]);
}

struct SegHeader {
    size_t hdr_offset = 0;
    size_t hdr_size = 0;
    uint8_t type = 0;
    uint32_t page = 0;
    uint32_t data_len = 0;
};

static std::optional<SegHeader> ParseSegHeader(std::span<const uint8_t> data, size_t pos) {
    if (pos + 6 > data.size()) return std::nullopt;
    const size_t start = pos;
    const uint32_t seg_num = ReadBE32(data, pos); pos += 4;
    const uint8_t flags = data[pos++];
    const uint8_t type = flags & 0x3F;
    const bool page_assoc_4b = (flags & 0x40) != 0;

    uint32_t ref_count = data[pos] >> 5;
    if (ref_count == 7) {
        if (pos + 4 > data.size()) return std::nullopt;
        ref_count = ReadBE32(data, pos) & 0x1FFFFFFFu;
        if (ref_count > 100000) return std::nullopt;
        const size_t retain_bytes = (static_cast<size_t>(ref_count) + 1 + 7) / 8;
        if (4 + retain_bytes > data.size() - pos) return std::nullopt;
        pos += 4 + retain_bytes;
    } else {
        pos += 1;
    }

    const size_t ref_bytes = (seg_num <= 256) ? 1 : (seg_num <= 65536 ? 2 : 4);
    const size_t refs_total = static_cast<size_t>(ref_count) * ref_bytes;
    if (refs_total > data.size() - pos) return std::nullopt;
    pos += refs_total;

    const size_t need = (page_assoc_4b ? 4u : 1u) + 4u;
    if (need > data.size() - pos) return std::nullopt;
    const uint32_t page = page_assoc_4b ? (pos += 4, ReadBE32(data, pos - 4)) : data[pos++];
    const uint32_t data_len = ReadBE32(data, pos); pos += 4;

    return SegHeader{ start, pos - start, type, page, data_len };
}

std::optional<ParsedFile> ParseStandaloneFile(std::span<const uint8_t> data) {
    if (data.size() < 9 || !std::equal(kSig.begin(), kSig.end(), data.begin())) return std::nullopt;

    const uint8_t flags = data[8];
    if ((flags & 0xFC) != 0) return std::nullopt;

    ParsedFile out;
    out.is_sequential = (flags & 0x01) != 0;
    size_t pos = 9;
    if ((flags & 0x02) == 0) {
        if (data.size() < 13) return std::nullopt;
        out.page_count = ReadBE32(data, 9);
        if (out.page_count > kMaxPages) return std::nullopt;
        pos = 13;
    }
    const size_t payload_start = pos;

    // Erst scannen (Zero-Copy-PrÃ¼fung + Validierung)
    struct SegRef { SegHeader h; size_t data_off; size_t data_sz; };
    std::vector<SegRef> segs;
    bool has_global = false;
    uint32_t max_page = 0;

    if (out.is_sequential) {
        while (pos < data.size()) {
            auto h = ParseSegHeader(data, pos);
            if (!h) break;
            pos += h->hdr_size;
            const size_t avail = data.size() - pos;
            const size_t len = (h->data_len == 0xFFFFFFFFu) ? avail : std::min<size_t>(h->data_len, avail);
            if (h->type == 51) break; // End of File
            if (h->page > kMaxPages) return std::nullopt;
            if (h->page == 0) has_global = true;
            else max_page = std::max(max_page, h->page);
            segs.push_back({ *h, pos, len });
            pos += len;
        }
    } else {
        std::vector<SegHeader> headers;
        while (pos < data.size()) {
            auto h = ParseSegHeader(data, pos);
            if (!h) break;
            pos += h->hdr_size;
            if (h->type == 51) break;
            if (h->page > kMaxPages) return std::nullopt;
            headers.push_back(*h);
        }
        for (const auto& h : headers) {
            if (h.data_len > data.size() - pos) return std::nullopt;
            if (h.page == 0) has_global = true;
            else max_page = std::max(max_page, h.page);
            segs.push_back({ h, pos, h.data_len });
            pos += h.data_len;
        }
    }

    if (max_page == 0) return std::nullopt;
    out.pages.resize(max_page);

    // Metadaten (AuflÃ¶sung, DPI, Striped-EndhÃ¶he) extrahieren
    for (const auto& s : segs) {
        if (s.h.page == 0) continue;
        auto& pm = out.pages[s.h.page - 1];
        if (s.h.type == 48 && s.data_sz >= 16) { // Page Information
            pm.width  = ReadBE32(data, s.data_off + 0);
            pm.height = ReadBE32(data, s.data_off + 4);
            const uint32_t x_ppm = ReadBE32(data, s.data_off + 8);
            const uint32_t y_ppm = ReadBE32(data, s.data_off + 12);
            pm.dpi_x = x_ppm ? (static_cast<double>(x_ppm) * 0.0254) : 96.0;
            pm.dpi_y = y_ppm ? (static_cast<double>(y_ppm) * 0.0254) : 96.0;
        } else if (s.h.type == 50 && s.data_sz >= 4 && pm.height == 0xFFFFFFFFu) { // End of Stripe
            pm.height = ReadBE32(data, s.data_off);
        }
    }

    // SicherheitsprÃ¼fung Dimensionen
    for (const auto& pm : out.pages) {
        if (pm.width == 0 || pm.height == 0 || pm.width > kMaxDim || pm.height > kMaxDim) return std::nullopt;
        if (static_cast<uint64_t>(pm.width) * pm.height > kMaxPixels) return std::nullopt;
    }

    // Zero-Copy Fast-Path: 1 Seite, Sequential, keine Global-Segmente
    if (out.is_sequential && max_page == 1 && !has_global && !segs.empty()) {
        out.zero_copy_single_page = true;
        out.single_page_slice = { payload_start, pos - payload_start };
    } else {
        out.page_streams.resize(max_page);
        for (const auto& s : segs) {
            auto& dst = (s.h.page == 0) ? out.global_stream : out.page_streams[s.h.page - 1];
            dst.insert(dst.end(), data.begin() + s.h.hdr_offset, data.begin() + s.h.hdr_offset + s.h.hdr_size);
            dst.insert(dst.end(), data.begin() + s.data_off, data.begin() + s.data_off + s.data_sz);
        }
    }

    out.page_count = max_page;
    return out;
}

} // namespace jbig2::detail