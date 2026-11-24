#include "jbig2_core.h"
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::vector<uint8_t> ReadAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

uint32_t ReadExpectedPages(const std::vector<uint8_t>& buf) {
    if (buf.size() >= 13 && (buf[8] & 0x02u) == 0u) {
        uint32_t n = (uint32_t(buf[9]) << 24) | (uint32_t(buf[10]) << 16) |
                     (uint32_t(buf[11]) << 8) | uint32_t(buf[12]);
        if (n > 0) return n;
    }
    return 1;
}

uint64_t DiffBitmaps(const jbig2::Bitmap1bpp& a, const jbig2::Bitmap1bpp& b) {
    uint64_t diff_bits = 0;
    const uint32_t full_bytes = a.width >> 3;
    const uint8_t tail_mask = (a.width & 7) ? static_cast<uint8_t>(0xFFu << (8 - (a.width & 7))) : 0;
    for (uint32_t y = 0; y < a.height; ++y) {
        const uint8_t* ra = a.pixels.data() + static_cast<size_t>(y) * a.stride;
        const uint8_t* rb = b.pixels.data() + static_cast<size_t>(y) * b.stride;
        for (uint32_t x = 0; x < full_bytes; ++x)
            diff_bits += static_cast<uint64_t>(std::popcount(static_cast<uint8_t>(ra[x] ^ rb[x])));
        if (tail_mask)
            diff_bits += static_cast<uint64_t>(std::popcount(static_cast<uint8_t>((ra[full_bytes] ^ rb[full_bytes]) & tail_mask)));
    }
    return diff_bits;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--compare") == 0) {
        auto ref_bytes = ReadAll(argv[2]);
        auto tst_bytes = ReadAll(argv[3]);
        const uint32_t ref_pages = ReadExpectedPages(ref_bytes);
        const uint32_t tst_pages = ReadExpectedPages(tst_bytes);
        if (ref_pages != tst_pages) {
            std::printf("FEHLER: Seitenzahl weicht ab (%u vs %u)\n", ref_pages, tst_pages);
            return 5;
        }
        uint64_t total_diff = 0;
        for (uint32_t p = 0; p < ref_pages; ++p) {
            auto a = jbig2::DecodePage(ref_bytes, p);
            auto b = jbig2::DecodePage(tst_bytes, p);
            if (!a || !b) {
                std::printf("FEHLER: Seite %u konnte nicht dekodiert werden.\n", p);
                return 2;
            }
            if (a->width != b->width || a->height != b->height) {
                std::printf("FEHLER: Groessenabweichung auf Seite %u (%ux%u vs %ux%u)\n",
                            p, a->width, a->height, b->width, b->height);
                return 5;
            }
            total_diff += DiffBitmaps(*a, *b);
        }
        std::printf("Seiten      : %u\n", ref_pages);
        std::printf("Abweichung  : %llu Bit(s)\n", static_cast<unsigned long long>(total_diff));
        return total_diff == 0 ? 0 : 6;
    }

    if (argc < 3) {
        std::printf("Usage: encoder_test.exe <in.jb2> <reencoded.jb2>\n"
                    "       encoder_test.exe --compare <ref.jb2> <test.jb2>\n");
        return 1;
    }

    auto orig_bytes = ReadAll(argv[1]);
    auto bmp1 = jbig2::DecodePage(orig_bytes, 0);
    if (!bmp1) { std::printf("FEHLER: Konnte %s nicht dekodieren.\n", argv[1]); return 2; }

    auto t0 = std::chrono::steady_clock::now();
    auto enc = jbig2::EncodeLosslessPage(*bmp1);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (enc.empty()) { std::printf("FEHLER: EncodeLosslessPage lieferte 0 Bytes.\n"); return 3; }

    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(enc.data()), static_cast<std::streamsize>(enc.size()));
    out.close();

    auto bmp2 = jbig2::DecodePage(enc, 0);
    if (!bmp2) { std::printf("FEHLER: PDFium konnte unsere encodierte Datei nicht lesen!\n"); return 4; }
    if (bmp1->width != bmp2->width || bmp1->height != bmp2->height) {
        std::printf("FEHLER: Groessenabweichung (%ux%u vs %ux%u)\n", bmp1->width, bmp1->height, bmp2->width, bmp2->height);
        return 5;
    }

    const uint64_t diff_bits = DiffBitmaps(*bmp1, *bmp2);
    std::printf("Bild        : %ux%u px (%.1f x %.1f DPI)\n", bmp1->width, bmp1->height, bmp1->dpi_x, bmp1->dpi_y);
    std::printf("Original    : %zu Bytes\n", orig_bytes.size());
    std::printf("Unser MQ-Enc: %zu Bytes (%.2f ms)\n", enc.size(), ms);
    std::printf("Abweichung  : %llu Bit(s)\n", static_cast<unsigned long long>(diff_bits));
    return diff_bits == 0 ? 0 : 6;
}
