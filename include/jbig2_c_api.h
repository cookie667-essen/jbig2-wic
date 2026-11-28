#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  define JBIG2_API
#  define JBIG2_CALL __stdcall
#else
#  define JBIG2_API __attribute__((visibility("default")))
#  define JBIG2_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define JBIG2_FLAG_AT_TUNING   1
#define JBIG2_FLAG_SYMBOL      2
#define JBIG2_FLAG_LOSSY_SAFE  4
#define JBIG2_FLAG_HALFTONE    8

typedef struct Jbig2PageInput {
    const uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    double dpi_x;
    double dpi_y;
} Jbig2PageInput;

typedef struct Jbig2PdfOutput {
    uint8_t*  globals_buf;
    size_t    globals_size;
    uint32_t  page_count;
    uint8_t** page_bufs;
    size_t*   page_sizes;
} Jbig2PdfOutput;

JBIG2_API int  JBIG2_CALL jbig2_encode_1bpp(
    const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride,
    double dpi_x, double dpi_y, uint8_t** out_buf, size_t* out_size);

JBIG2_API void JBIG2_CALL jbig2_free_buffer(uint8_t* buf);

JBIG2_API int  JBIG2_CALL jbig2_encode_multipage_1bpp(
    const Jbig2PageInput* pages, uint32_t page_count, int flags,
    uint8_t** out_buf, size_t* out_size);

JBIG2_API int  JBIG2_CALL jbig2_encode_pdf_1bpp(
    const Jbig2PageInput* pages, uint32_t page_count, int flags,
    Jbig2PdfOutput* out_pdf);

JBIG2_API void JBIG2_CALL jbig2_free_pdf_output(Jbig2PdfOutput* pdf);

#ifdef __cplusplus
}
#endif
