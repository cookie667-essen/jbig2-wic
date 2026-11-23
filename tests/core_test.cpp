#include "jbig2_core.h"
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    std::printf("[A] Gelesen: %zu Bytes\n", data.size()); std::fflush(stdout);

    auto info = jbig2::InspectFile(data);
    std::printf("[B] InspectFile: ok=%d pages=%u seq=%d\n",
                (int)info.has_value(), info ? info->page_count : 0, info ? (int)info->is_sequential : 0);
    std::fflush(stdout);
    if (!info) return 2;

    auto bmp = jbig2::DecodePage(data, 0);
    std::printf("[C] DecodePage(0): ok=%d %ux%u stride=%u\n",
                (int)bmp.has_value(), bmp ? bmp->width : 0, bmp ? bmp->height : 0, bmp ? bmp->stride : 0);
    std::fflush(stdout);
    return bmp ? 0 : 3;
}