#include "jbig2_c_api.h"
#include "jbig2_core.h"
#include <cstdlib>
#include <cstring>
#include <vector>

static inline bool ValidPage(const uint8_t* px, uint32_t w, uint32_t h, uint32_t stride) {
    return px && w > 0 && h > 0 && stride >= (w + 7u) / 8u;
}

extern "C" {

JBIG2_API int JBIG2_CALL jbig2_encode_1bpp(
    const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride,
    double dpi_x, double dpi_y, uint8_t** out_buf, size_t* out_size) {
    if (!ValidPage(pixels, width, height, stride) || !out_buf || !out_size) return -1;
    *out_buf = nullptr; *out_size = 0;
    try {
        jbig2::Bitmap1bppView v{
            pixels, static_cast<size_t>(stride) * height, width, height, stride, dpi_x, dpi_y
        };
        auto encoded = jbig2::EncodeLosslessDocumentView(std::span<const jbig2::Bitmap1bppView>(&v, 1));
        if (encoded.empty()) return -2;
        auto* raw = static_cast<uint8_t*>(std::malloc(encoded.size()));
        if (!raw) return -3;
        std::memcpy(raw, encoded.data(), encoded.size());
        *out_buf = raw;
        *out_size = encoded.size();
        return 0;
    } catch (...) {
        return -4;
    }
}

JBIG2_API void JBIG2_CALL jbig2_free_buffer(uint8_t* buf) {
    std::free(buf);
}

JBIG2_API int JBIG2_CALL jbig2_encode_multipage_1bpp(
    const Jbig2PageInput* pages, uint32_t page_count, int flags,
    uint8_t** out_buf, size_t* out_size) {
    if (!pages || page_count == 0 || !out_buf || !out_size) return -1;
    *out_buf = nullptr; *out_size = 0;
    try {
        std::vector<jbig2::Bitmap1bppView> views;
        views.reserve(page_count);
        for (uint32_t i = 0; i < page_count; ++i) {
            if (!ValidPage(pages[i].pixels, pages[i].width, pages[i].height, pages[i].stride)) return -1;
            views.push_back({
                pages[i].pixels, static_cast<size_t>(pages[i].stride) * pages[i].height,
                pages[i].width, pages[i].height, pages[i].stride, pages[i].dpi_x, pages[i].dpi_y
            });
        }
        jbig2::LosslessEncodeOptions opts;
        opts.adaptive_templates = ((flags & JBIG2_FLAG_AT_TUNING) != 0);
        opts.symbol_coding      = ((flags & JBIG2_FLAG_SYMBOL) != 0);
        opts.lossy_symbol       = ((flags & JBIG2_FLAG_LOSSY_SAFE) != 0);
        opts.halftone_coding    = ((flags & JBIG2_FLAG_HALFTONE) != 0);
        auto encoded = jbig2::EncodeLosslessDocumentView(views, opts);
        if (encoded.empty()) return -2;
        auto* raw = static_cast<uint8_t*>(std::malloc(encoded.size()));
        if (!raw) return -3;
        std::memcpy(raw, encoded.data(), encoded.size());
        *out_buf = raw;
        *out_size = encoded.size();
        return 0;
    } catch (...) {
        return -4;
    }
}

JBIG2_API void JBIG2_CALL jbig2_free_pdf_output(Jbig2PdfOutput* pdf) {
    if (!pdf) return;
    std::free(pdf->globals_buf);
    if (pdf->page_bufs) {
        for (uint32_t i = 0; i < pdf->page_count; ++i) std::free(pdf->page_bufs[i]);
        std::free(pdf->page_bufs);
    }
    std::free(pdf->page_sizes);
    std::memset(pdf, 0, sizeof(*pdf));
}

JBIG2_API int JBIG2_CALL jbig2_encode_pdf_1bpp(
    const Jbig2PageInput* pages, uint32_t page_count, int flags, Jbig2PdfOutput* out_pdf) {
    if (!pages || page_count == 0 || !out_pdf) return -1;
    std::memset(out_pdf, 0, sizeof(*out_pdf));
    try {
        std::vector<jbig2::Bitmap1bppView> views;
        views.reserve(page_count);
        for (uint32_t i = 0; i < page_count; ++i) {
            if (!ValidPage(pages[i].pixels, pages[i].width, pages[i].height, pages[i].stride)) return -1;
            views.push_back({
                pages[i].pixels, static_cast<size_t>(pages[i].stride) * pages[i].height,
                pages[i].width, pages[i].height, pages[i].stride, pages[i].dpi_x, pages[i].dpi_y
            });
        }
        jbig2::LosslessEncodeOptions opts;
        opts.adaptive_templates = ((flags & JBIG2_FLAG_AT_TUNING) != 0);
        opts.symbol_coding      = ((flags & JBIG2_FLAG_SYMBOL) != 0);
        opts.lossy_symbol       = ((flags & JBIG2_FLAG_LOSSY_SAFE) != 0);
        opts.halftone_coding    = ((flags & JBIG2_FLAG_HALFTONE) != 0);
        auto pdf = jbig2::EncodePdfDocumentView(views, opts);
        if (pdf.pages.size() != page_count) return -2;
        out_pdf->page_count = page_count;
        out_pdf->page_bufs  = static_cast<uint8_t**>(std::calloc(page_count, sizeof(uint8_t*)));
        out_pdf->page_sizes = static_cast<size_t*>(std::calloc(page_count, sizeof(size_t)));
        if (!out_pdf->page_bufs || !out_pdf->page_sizes) { jbig2_free_pdf_output(out_pdf); return -3; }
        if (!pdf.globals.empty()) {
            out_pdf->globals_buf = static_cast<uint8_t*>(std::malloc(pdf.globals.size()));
            if (!out_pdf->globals_buf) { jbig2_free_pdf_output(out_pdf); return -3; }
            std::memcpy(out_pdf->globals_buf, pdf.globals.data(), pdf.globals.size());
            out_pdf->globals_size = pdf.globals.size();
        }
        for (uint32_t i = 0; i < page_count; ++i) {
            const auto& ps = pdf.pages[i];
            if (!ps.empty()) {
                out_pdf->page_bufs[i] = static_cast<uint8_t*>(std::malloc(ps.size()));
                if (!out_pdf->page_bufs[i]) { jbig2_free_pdf_output(out_pdf); return -3; }
                std::memcpy(out_pdf->page_bufs[i], ps.data(), ps.size());
                out_pdf->page_sizes[i] = ps.size();
            }
        }
        return 0;
    } catch (...) {
        jbig2_free_pdf_output(out_pdf);
        return -4;
    }
}

} // extern "C"
