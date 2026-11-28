#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jbig2::detail {

struct PageMeta {
    uint32_t width = 0;
    uint32_t height = 0;
    double dpi_x = 96.0;
    double dpi_y = 96.0;
};

struct Slice { size_t offset = 0; size_t length = 0; };

struct ParsedFile {
    bool is_sequential = true;
    bool zero_copy_single_page = false;
    Slice single_page_slice{};
    uint32_t page_count = 0;
    std::vector<uint8_t> global_stream;
    std::vector<std::vector<uint8_t>> page_streams;
    std::vector<PageMeta> pages;
};

std::optional<ParsedFile> ParseStandaloneFile(std::span<const uint8_t> data);

} // namespace jbig2::detail
