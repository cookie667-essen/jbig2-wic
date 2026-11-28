#include "jbig2_core.h"
#include "core/fxcodec/jbig2/jbig2_decoder.h"
#include "core/fxcodec/jbig2/jbig2_document_context.h"

#include "jbig2_file_parser.h"

namespace jbig2 {

struct Document::Impl {
    std::vector<uint8_t> raw_bytes;
    detail::ParsedFile parsed;
};

Document::Document() = default;
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

std::optional<Document> Document::Open(std::vector<uint8_t> file_bytes) {
    try {
        auto parsed = detail::ParseStandaloneFile(file_bytes);
        if (!parsed) return std::nullopt;
        Document doc;
        doc.impl_ = std::make_unique<Impl>(Impl{ std::move(file_bytes), std::move(*parsed) });
        return doc;
    } catch (...) {
        return std::nullopt;
    }
}

uint32_t Document::PageCount() const { return impl_ ? impl_->parsed.page_count : 0; }
bool Document::IsSequential() const  { return impl_ ? impl_->parsed.is_sequential : true; }

std::optional<Bitmap1bpp> Document::DecodePage(uint32_t page_index) const {
    if (!impl_ || page_index >= impl_->parsed.pages.size()) return std::nullopt;
    try {
        const auto& parsed = impl_->parsed;
        const auto& meta = parsed.pages[page_index];

        Bitmap1bpp bmp;
        bmp.width  = meta.width;
        bmp.height = meta.height;
        bmp.stride = ((meta.width + 31) / 32) * 4; // 4-Byte-Alignment fÃƒÂ¼r PDFium
        bmp.dpi_x  = meta.dpi_x;
        bmp.dpi_y  = meta.dpi_y;
        bmp.pixels.assign(static_cast<size_t>(bmp.stride) * bmp.height, 0xFF);

        pdfium::span<const uint8_t> page_span;
        if (parsed.zero_copy_single_page) {
            page_span = pdfium::span<const uint8_t>(impl_->raw_bytes)
                            .subspan(parsed.single_page_slice.offset, parsed.single_page_slice.length);
        } else {
            page_span = parsed.page_streams[page_index];
        }

        fxcodec::Jbig2Context ctx{};
        JBig2_DocumentContext doc_ctx;
        auto status = fxcodec::Jbig2Decoder::StartDecode(
            &ctx, &doc_ctx, bmp.width, bmp.height,
            page_span, page_index + 1,
            parsed.global_stream, 0,
            bmp.pixels, bmp.stride, nullptr, false
        );
        while (status == FXCODEC_STATUS::kDecodeToBeContinued) {
            status = fxcodec::Jbig2Decoder::ContinueDecode(&ctx, nullptr);
        }
        if (status != FXCODEC_STATUS::kDecodeFinished) return std::nullopt;
        return bmp;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<DocumentInfo> InspectFile(std::span<const uint8_t> file_bytes) {
    try {
        auto parsed = detail::ParseStandaloneFile(file_bytes);
        if (!parsed) return std::nullopt;
        return DocumentInfo{ parsed->page_count, parsed->is_sequential };
    } catch (...) { return std::nullopt; }
}

std::optional<Bitmap1bpp> DecodePage(std::span<const uint8_t> file_bytes, uint32_t page_index) {
    auto doc = Document::Open(std::vector<uint8_t>(file_bytes.begin(), file_bytes.end()));
    return doc ? doc->DecodePage(page_index) : std::nullopt;
}



} // namespace jbig2