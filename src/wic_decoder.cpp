#include "jbig2_core.h"
#include "wic_guids.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <atomic>
#include <cstring>

using Microsoft::WRL::ChainInterfaces;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

extern std::atomic<long> g_comObjects;

class Jbig2FrameDecode final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                          ChainInterfaces<IWICBitmapFrameDecode, IWICBitmapSource>> {
    jbig2::Bitmap1bpp bmp_;

public:
    explicit Jbig2FrameDecode(jbig2::Bitmap1bpp bmp) : bmp_(std::move(bmp)) {
        g_comObjects.fetch_add(1, std::memory_order_relaxed);
    }
    ~Jbig2FrameDecode() override {
        g_comObjects.fetch_sub(1, std::memory_order_acq_rel);
    }

    IFACEMETHODIMP GetSize(UINT* w, UINT* h) override {
        if (!w || !h) return E_INVALIDARG;
        *w = bmp_.width; *h = bmp_.height;
        return S_OK;
    }
    IFACEMETHODIMP GetPixelFormat(WICPixelFormatGUID* fmt) override {
        if (!fmt) return E_INVALIDARG;
        *fmt = GUID_WICPixelFormatBlackWhite;
        return S_OK;
    }
    IFACEMETHODIMP GetResolution(double* dpiX, double* dpiY) override {
        if (!dpiX || !dpiY) return E_INVALIDARG;
        *dpiX = bmp_.dpi_x; *dpiY = bmp_.dpi_y;
        return S_OK;
    }
    IFACEMETHODIMP CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        if (!pbBuffer) return E_INVALIDARG;
        WICRect rc = prc ? *prc : WICRect{ 0, 0, static_cast<INT>(bmp_.width), static_cast<INT>(bmp_.height) };
        if (rc.X < 0 || rc.Y < 0 || rc.Width < 0 || rc.Height < 0 ||
            static_cast<int64_t>(rc.X) + rc.Width > static_cast<int64_t>(bmp_.width) ||
            static_cast<int64_t>(rc.Y) + rc.Height > static_cast<int64_t>(bmp_.height)) return E_INVALIDARG;
        if (rc.Width == 0 || rc.Height == 0) return S_OK;

        const UINT row_bytes = (static_cast<UINT>(rc.Width) + 7) / 8;
        const uint64_t min_buf = static_cast<uint64_t>(cbStride) * (static_cast<uint64_t>(rc.Height) - 1) + row_bytes;
        if (cbStride < row_bytes || cbBufferSize < min_buf)
            return WINCODEC_ERR_INSUFFICIENTBUFFER;

        if (rc.X == 0) {
            for (INT y = 0; y < rc.Height; ++y) {
                const uint8_t* src = bmp_.pixels.data() + static_cast<size_t>(rc.Y + y) * bmp_.stride;
                std::memcpy(pbBuffer + static_cast<size_t>(y) * cbStride, src, row_bytes);
            }
            return S_OK;
        }
        const uint32_t byte_off = static_cast<uint32_t>(rc.X) / 8;
        const uint32_t bit_off  = static_cast<uint32_t>(rc.X) % 8;
        for (INT y = 0; y < rc.Height; ++y) {
            const uint8_t* src = bmp_.pixels.data() + static_cast<size_t>(rc.Y + y) * bmp_.stride + byte_off;
            uint8_t* dst = pbBuffer + static_cast<size_t>(y) * cbStride;
            for (UINT b = 0; b < row_bytes; ++b) {
                uint8_t hi = static_cast<uint8_t>(src[b] << bit_off);
                uint8_t lo = (bit_off && (byte_off + b + 1 < bmp_.stride))
                             ? static_cast<uint8_t>(src[b + 1] >> (8 - bit_off)) : 0;
                dst[b] = hi | lo;
            }
        }
        return S_OK;
    }
    IFACEMETHODIMP CopyPalette(IWICPalette*) override { return WINCODEC_ERR_PALETTEUNAVAILABLE; }
    IFACEMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader** pp) override {
        if (pp) *pp = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    IFACEMETHODIMP GetColorContexts(UINT, IWICColorContext**, UINT* actual) override {
        if (!actual) return E_INVALIDARG;
        *actual = 0;
        return S_OK;
    }
    IFACEMETHODIMP GetThumbnail(IWICBitmapSource** pp) override {
        if (pp) *pp = nullptr;
        return WINCODEC_ERR_CODECNOTHUMBNAIL;
    }
};

HRESULT CreateJbig2FrameDecode(jbig2::Bitmap1bpp bmp, IWICBitmapFrameDecode** ppFrame) {
    if (!ppFrame) return E_INVALIDARG;
    *ppFrame = nullptr;
    auto frame = Make<Jbig2FrameDecode>(std::move(bmp));
    return frame ? frame.CopyTo(ppFrame) : E_OUTOFMEMORY;
}