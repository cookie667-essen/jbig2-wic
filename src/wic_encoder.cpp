#include "jbig2_core.h"
#include "wic_guids.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

extern std::atomic<long> g_comObjects;

namespace {

struct SharedEncoderState {
    std::mutex mtx;
    std::vector<jbig2::Bitmap1bpp> pages;
    bool committed = false;
};

class Jbig2FrameEncode final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmapFrameEncode> {
    std::shared_ptr<SharedEncoderState> state_;
    jbig2::Bitmap1bpp bmp_;
    UINT cur_line_ = 0;
    bool initialized_ = false;
    bool size_set_ = false;
    bool resolution_set_ = false;
    bool committed_ = false;

public:
    explicit Jbig2FrameEncode(std::shared_ptr<SharedEncoderState> st) : state_(std::move(st)) {
        g_comObjects.fetch_add(1, std::memory_order_relaxed);
    }
    ~Jbig2FrameEncode() override {
        g_comObjects.fetch_sub(1, std::memory_order_acq_rel);
    }

    IFACEMETHODIMP Initialize(IPropertyBag2*) override {
        initialized_ = true;
        return S_OK;
    }
    IFACEMETHODIMP SetSize(UINT uiWidth, UINT uiHeight) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (uiWidth == 0 || uiHeight == 0 || uiWidth > 65536 || uiHeight > 65536) return E_INVALIDARG;
        bmp_.width  = uiWidth;
        bmp_.height = uiHeight;
        bmp_.stride = (uiWidth + 7) / 8;
        bmp_.pixels.assign(static_cast<size_t>(bmp_.stride) * bmp_.height, 0xFF);
        cur_line_ = 0;
        size_set_ = true;
        return S_OK;
    }
    IFACEMETHODIMP SetResolution(double dpiX, double dpiY) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        bmp_.dpi_x = dpiX > 0.0 ? dpiX : 96.0;
        bmp_.dpi_y = dpiY > 0.0 ? dpiY : 96.0;
        resolution_set_ = true;
        return S_OK;
    }
    IFACEMETHODIMP SetPixelFormat(WICPixelFormatGUID* pPixelFormat) override {
        if (!pPixelFormat) return E_INVALIDARG;
        *pPixelFormat = GUID_WICPixelFormatBlackWhite;
        return S_OK;
    }
    IFACEMETHODIMP SetColorContexts(UINT, IWICColorContext**) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP SetPalette(IWICPalette*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP SetThumbnail(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }

    IFACEMETHODIMP WritePixels(UINT lineCount, UINT cbStride, UINT cbBufferSize, BYTE* pbPixels) override {
        if (!initialized_ || !size_set_) return WINCODEC_ERR_NOTINITIALIZED;
        if (committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!pbPixels) return E_INVALIDARG;
        if (lineCount == 0) return S_OK;
        if (cur_line_ + lineCount > bmp_.height) return E_INVALIDARG;

        const uint64_t min_buf = static_cast<uint64_t>(cbStride) * (lineCount - 1) + bmp_.stride;
        if (cbStride < bmp_.stride || cbBufferSize < min_buf) return WINCODEC_ERR_INSUFFICIENTBUFFER;

        for (UINT y = 0; y < lineCount; ++y) {
            std::memcpy(bmp_.pixels.data() + static_cast<size_t>(cur_line_ + y) * bmp_.stride,
                        pbPixels + static_cast<size_t>(y) * cbStride, bmp_.stride);
        }
        cur_line_ += lineCount;
        return S_OK;
    }

    IFACEMETHODIMP WriteSource(IWICBitmapSource* pIBitmapSource, WICRect* prc) override {
        if (!initialized_) return WINCODEC_ERR_NOTINITIALIZED;
        if (committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!pIBitmapSource) return E_INVALIDARG;

        UINT sw = 0, sh = 0;
        HRESULT hr = pIBitmapSource->GetSize(&sw, &sh);
        if (FAILED(hr)) return hr;

        WICRect rc = prc ? *prc : WICRect{ 0, 0, static_cast<INT>(sw), static_cast<INT>(sh) };
        if (rc.X < 0 || rc.Y < 0 || rc.Width <= 0 || rc.Height <= 0 ||
            rc.X + rc.Width > static_cast<INT>(sw) || rc.Y + rc.Height > static_cast<INT>(sh))
            return E_INVALIDARG;

        if (!size_set_) {
            hr = SetSize(static_cast<UINT>(rc.Width), static_cast<UINT>(rc.Height));
            if (FAILED(hr)) return hr;
        }
        double dx = 96.0, dy = 96.0;
        if (!resolution_set_ && SUCCEEDED(pIBitmapSource->GetResolution(&dx, &dy))) {
            bmp_.dpi_x = dx; bmp_.dpi_y = dy;
        }

        // Quelle ggf. automatisch nach 1bpp BlackWhite konvertieren
        ComPtr<IWICBitmapSource> src = pIBitmapSource;
        WICPixelFormatGUID fmt{};
        if (SUCCEEDED(pIBitmapSource->GetPixelFormat(&fmt)) && fmt != GUID_WICPixelFormatBlackWhite) {
            ComPtr<IWICImagingFactory> factory;
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (FAILED(hr)) return hr;
            ComPtr<IWICFormatConverter> conv;
            hr = factory->CreateFormatConverter(&conv);
            if (FAILED(hr)) return hr;
            hr = conv->Initialize(pIBitmapSource, GUID_WICPixelFormatBlackWhite,
                                  WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
            if (FAILED(hr)) return hr;
            src = conv;
        }

        hr = src->CopyPixels(&rc, bmp_.stride, static_cast<UINT>(bmp_.pixels.size()), bmp_.pixels.data());
        if (SUCCEEDED(hr)) cur_line_ = bmp_.height;
        return hr;
    }

    IFACEMETHODIMP Commit() override {
        if (!initialized_ || !size_set_ || committed_) return WINCODEC_ERR_WRONGSTATE;
        std::scoped_lock lock(state_->mtx);
        if (state_->committed) return WINCODEC_ERR_WRONGSTATE;
        state_->pages.push_back(std::move(bmp_));
        committed_ = true;
        return S_OK;
    }

    IFACEMETHODIMP GetMetadataQueryWriter(IWICMetadataQueryWriter** pp) override {
        if (pp) *pp = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
};

class Jbig2BitmapEncoder final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmapEncoder> {
    std::shared_ptr<SharedEncoderState> state_ = std::make_shared<SharedEncoderState>();
    ComPtr<IStream> stream_;

public:
    Jbig2BitmapEncoder() { g_comObjects.fetch_add(1, std::memory_order_relaxed); }
    ~Jbig2BitmapEncoder() override { g_comObjects.fetch_sub(1, std::memory_order_acq_rel); }

    IFACEMETHODIMP Initialize(IStream* pIStream, WICBitmapEncoderCacheOption) override {
        if (!pIStream) return E_INVALIDARG;
        std::scoped_lock lock(state_->mtx);
        stream_ = pIStream;
        state_->pages.clear();
        state_->committed = false;
        return S_OK;
    }
    IFACEMETHODIMP GetContainerFormat(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_ContainerFormatJbig2;
        return S_OK;
    }
    IFACEMETHODIMP GetEncoderInfo(IWICBitmapEncoderInfo** ppIEncoderInfo) override {
        if (!ppIEncoderInfo) return E_INVALIDARG;
        *ppIEncoderInfo = nullptr;
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        ComPtr<IWICComponentInfo> comp;
        hr = factory->CreateComponentInfo(CLSID_Jbig2Encoder, &comp);
        return SUCCEEDED(hr) ? comp.CopyTo(ppIEncoderInfo) : hr;
    }
    IFACEMETHODIMP SetColorContexts(UINT, IWICColorContext**) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP SetPalette(IWICPalette*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP SetThumbnail(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP SetPreview(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }

    IFACEMETHODIMP CreateNewFrame(IWICBitmapFrameEncode** ppIFrameEncode, IPropertyBag2** ppIEncoderOptions) override {
        if (!ppIFrameEncode) return E_INVALIDARG;
        *ppIFrameEncode = nullptr;
        if (ppIEncoderOptions) *ppIEncoderOptions = nullptr;
        if (!stream_) return WINCODEC_ERR_NOTINITIALIZED;
        auto frame = Make<Jbig2FrameEncode>(state_);
        return frame ? frame.CopyTo(ppIFrameEncode) : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP Commit() override {
        std::scoped_lock lock(state_->mtx);
        if (!stream_ || state_->committed || state_->pages.empty()) return WINCODEC_ERR_WRONGSTATE;

        auto encoded = jbig2::EncodeLosslessDocument(state_->pages);
        if (encoded.empty()) return E_FAIL;

        size_t offset = 0;
        while (offset < encoded.size()) {
            const ULONG chunk = static_cast<ULONG>(std::min<size_t>(encoded.size() - offset, 0x1000000u));
            ULONG written = 0;
            HRESULT hr = stream_->Write(encoded.data() + offset, chunk, &written);
            if (FAILED(hr) || written == 0) return FAILED(hr) ? hr : WINCODEC_ERR_STREAMWRITE;
            offset += written;
        }
        state_->committed = true;
        return S_OK;
    }

    IFACEMETHODIMP GetMetadataQueryWriter(IWICMetadataQueryWriter** pp) override {
        if (pp) *pp = nullptr;
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
};

} // namespace

HRESULT CreateJbig2BitmapEncoder(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    auto enc = Make<Jbig2BitmapEncoder>();
    return enc ? enc.CopyTo(riid, ppv) : E_OUTOFMEMORY;
}