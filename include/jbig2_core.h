#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace jbig2 {

struct Bitmap1bpp {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    double dpi_x = 96.0;
    double dpi_y = 96.0;
    std::vector<uint8_t> pixels;
};

struct Bitmap1bppView { const uint8_t* pixels = nullptr; size_t pixel_bytes = 0; uint32_t width = 0, height = 0, stride = 0; double dpi_x = 96.0, dpi_y = 96.0; };

struct DocumentInfo {
    uint32_t page_count = 0;
    bool is_sequential = true;
};

class Document {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;

    static std::optional<Document> Open(std::vector<uint8_t> file_bytes);
    uint32_t PageCount() const;
    bool IsSequential() const;
    std::optional<Bitmap1bpp> DecodePage(uint32_t page_index) const;
};

std::optional<DocumentInfo> InspectFile(std::span<const uint8_t> file_bytes);
std::optional<Bitmap1bpp>   DecodePage(std::span<const uint8_t> file_bytes, uint32_t page_index);

struct LosslessEncodeOptions {
    uint8_t template_id = 0;
    bool adaptive_templates = false; // Stufe B: Auto-AT-Optimierung fuer Dither/Raster
    bool symbol_coding = false; // Lossless Symbol Dictionary + Text Region
    bool lossy_symbol = false;
    bool halftone_coding = false; // Lossless 4x4 Halftone Region (Seg 16 + 23)  // Opt-in: Safe Lossy Symbol Coding (SBREFINE=0 + 4-stage Xerox guard)
};

std::vector<uint8_t> EncodeLosslessPage(const Bitmap1bpp& bitmap, LosslessEncodeOptions opts = {});
std::vector<uint8_t> EncodeLosslessDocumentView(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts = {});
std::vector<uint8_t> EncodeLosslessDocument(std::span<const Bitmap1bpp> pages, LosslessEncodeOptions opts = {});


struct PdfEncodedDocument {
    std::vector<uint8_t> globals;
    std::vector<std::vector<uint8_t>> pages;
};

PdfEncodedDocument EncodePdfDocumentView(std::span<const Bitmap1bppView> pages, LosslessEncodeOptions opts = {});
PdfEncodedDocument EncodePdfDocument(std::span<const Bitmap1bpp> pages, LosslessEncodeOptions opts = {});

} // namespace jbig2