#include "jbig2_core.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: encoder_test.exe <in.jb2> <reencoded.jb2>\n");
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> orig_bytes((std::istreambuf_iterator<char>(in)), {});
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

    // Gegenprobe: mit PDFium zurÃ¼ckdekodieren & jedes Pixelbit vergleichen
    auto bmp2 = jbig2::DecodePage(enc, 0);
    if (!bmp2) { std::printf("FEHLER: PDFium konnte unsere encodierte Datei nicht lesen!\n"); return 4; }
    if (bmp1->width != bmp2->width || bmp1->height != bmp2->height) {
        std::printf("FEHLER: Groessenabweichung (%ux%u vs %ux%u)\n", bmp1->width, bmp1->height, bmp2->width, bmp2->height);
        return 5;
    }

    uint64_t diff_bits = 0;
    const uint32_t full_bytes = bmp1->width >> 3;
    const uint8_t tail_mask = (bmp1->width & 7) ? static_cast<uint8_t>(0xFFu << (8 - (bmp1->width & 7))) : 0;
    for (uint32_t y = 0; y < bmp1->height; ++y) {
        const uint8_t* a = bmp1->pixels.data() + static_cast<size_t>(y) * bmp1->stride;
        const uint8_t* b = bmp2->pixels.data() + static_cast<size_t>(y) * bmp2->stride;
        for (uint32_t x = 0; x < full_bytes; ++x) {
            uint8_t d = a[x] ^ b[x];
            while (d) { diff_bits += (d & 1); d >>= 1; }
        }
        if (tail_mask) {
            uint8_t d = (a[full_bytes] ^ b[full_bytes]) & tail_mask;
            while (d) { diff_bits += (d & 1); d >>= 1; }
        }
    }

    std::printf("Bild        : %ux%u px (%.1f x %.1f DPI)\n", bmp1->width, bmp1->height, bmp1->dpi_x, bmp1->dpi_y);
    std::printf("Original    : %zu Bytes\n", orig_bytes.size());
    std::printf("Unser MQ-Enc: %zu Bytes (%.2f ms)\n", enc.size(), ms);
    std::printf("Abweichung  : %llu Bit(s)\n", static_cast<unsigned long long>(diff_bits));
    return diff_bits == 0 ? 0 : 6;
}