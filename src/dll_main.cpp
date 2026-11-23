#include "jbig2_core.h"
#include "wic_guids.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

HRESULT CreateJbig2BitmapEncoder(REFIID riid, void** ppv);
HRESULT CreateJbig2FrameDecode(jbig2::Bitmap1bpp bmp, IWICBitmapFrameDecode** ppFrame);
HRESULT RegisterJbig2WicCodec(HMODULE hModule);
HRESULT UnregisterJbig2WicCodec();

static HMODULE g_hModule = nullptr;
std::atomic<long> g_comObjects{ 0 };

class Jbig2BitmapDecoder final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IWICBitmapDecoder> {
    std::mutex mtx_;
    std::optional<jbig2::Document> doc_;
    UINT cached_idx_ = UINT_MAX;
    ComPtr<IWICBitmapFrameDecode> cached_frame_;

    static HRESULT ReadAllStream(IStream* stream, std::vector<uint8_t>& out) {
        constexpr size_t kMaxFileBytes = 256ull * 1024 * 1024;
        LARGE_INTEGER zero{};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        out.clear();
        STATSTG st{}; if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0 && st.cbSize.QuadPart <= kMaxFileBytes) out.reserve(static_cast<size_t>(st.cbSize.QuadPart));
        uint8_t buf[65536];
        ULONG read = 0;
        while (SUCCEEDED(stream->Read(buf, sizeof(buf), &read)) && read > 0) {
            if (out.size() + read > kMaxFileBytes) return WINCODEC_ERR_STREAMREAD;
            out.insert(out.end(), buf, buf + read);
        }
        return out.empty() ? WINCODEC_ERR_BADHEADER : S_OK;
    }

public:
    Jbig2BitmapDecoder() { g_comObjects.fetch_add(1, std::memory_order_relaxed); }
    ~Jbig2BitmapDecoder() override { g_comObjects.fetch_sub(1, std::memory_order_acq_rel); }

    IFACEMETHODIMP QueryCapability(IStream* pIStream, DWORD* pdwCapability) override {
        if (!pIStream || !pdwCapability) return E_INVALIDARG;
        *pdwCapability = 0;

        LARGE_INTEGER zero{};
        ULARGE_INTEGER orig_pos{};
        pIStream->Seek(zero, STREAM_SEEK_CUR, &orig_pos);

        uint8_t sig[8]{};
        ULONG read = 0;
        pIStream->Seek(zero, STREAM_SEEK_SET, nullptr);
        pIStream->Read(sig, sizeof(sig), &read);

        LARGE_INTEGER restore{};
        restore.QuadPart = static_cast<LONGLONG>(orig_pos.QuadPart);
        pIStream->Seek(restore, STREAM_SEEK_SET, nullptr);

        static constexpr uint8_t kExpected[8] = { 0x97, 0x4A, 0x42, 0x32, 0x0D, 0x0A, 0x1A, 0x0A };
        if (read == 8 && std::memcmp(sig, kExpected, 8) == 0) {
            *pdwCapability = WICBitmapDecoderCapabilityCanDecodeAllImages |
                             WICBitmapDecoderCapabilityCanDecodeSomeImages;
        }
        return S_OK;
    }

    IFACEMETHODIMP Initialize(IStream* pIStream, WICDecodeOptions) override {
        if (!pIStream) return E_INVALIDARG;
        std::scoped_lock lock(mtx_);
        std::vector<uint8_t> bytes;
        HRESULT hr = ReadAllStream(pIStream, bytes);
        if (FAILED(hr)) return hr;

        auto doc = jbig2::Document::Open(std::move(bytes));
        if (!doc || doc->PageCount() == 0) return WINCODEC_ERR_BADHEADER;

        doc_ = std::move(doc);
        cached_idx_ = UINT_MAX;
        cached_frame_.Reset();
        return S_OK;
    }

    IFACEMETHODIMP GetContainerFormat(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_ContainerFormatJbig2;
        return S_OK;
    }
    IFACEMETHODIMP GetDecoderInfo(IWICBitmapDecoderInfo** ppIDecoderInfo) override {
        if (!ppIDecoderInfo) return E_INVALIDARG;
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        ComPtr<IWICComponentInfo> comp;
        hr = factory->CreateComponentInfo(CLSID_Jbig2Decoder, &comp);
        return SUCCEEDED(hr) ? comp.CopyTo(ppIDecoderInfo) : hr;
    }
    IFACEMETHODIMP GetFrameCount(UINT* pCount) override {
        if (!pCount) return E_INVALIDARG;
        std::scoped_lock lock(mtx_);
        *pCount = doc_ ? doc_->PageCount() : 0;
        return S_OK;
    }
    IFACEMETHODIMP GetFrame(UINT index, IWICBitmapFrameDecode** ppIBitmapFrame) override {
        if (!ppIBitmapFrame) return E_INVALIDARG;
        std::scoped_lock lock(mtx_);
        if (!doc_ || index >= doc_->PageCount()) return WINCODEC_ERR_FRAMEMISSING;

        if (cached_idx_ == index && cached_frame_) {
            return cached_frame_.CopyTo(ppIBitmapFrame);
        }
        auto bmp = doc_->DecodePage(index);
        if (!bmp) return WINCODEC_ERR_BADIMAGE;

        ComPtr<IWICBitmapFrameDecode> frame;
        HRESULT hr = CreateJbig2FrameDecode(std::move(*bmp), &frame);
        if (FAILED(hr)) return hr;
        cached_idx_ = index;
        cached_frame_ = frame;
        return frame.CopyTo(ppIBitmapFrame);
    }
    IFACEMETHODIMP CopyPalette(IWICPalette*) override { return WINCODEC_ERR_PALETTEUNAVAILABLE; }
    IFACEMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader** pp) override { if (pp) *pp = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetPreview(IWICBitmapSource** pp) override { if (pp) *pp = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetColorContexts(UINT, IWICColorContext**, UINT* c) override { if (!c) return E_INVALIDARG; *c = 0; return S_OK; }
    IFACEMETHODIMP GetThumbnail(IWICBitmapSource** pp) override { if (pp) *pp = nullptr; return WINCODEC_ERR_CODECNOTHUMBNAIL; }
};

class Jbig2ClassFactory final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory> {
    bool is_encoder_ = false;
public:
    explicit Jbig2ClassFactory(bool is_encoder) : is_encoder_(is_encoder) {
        g_comObjects.fetch_add(1, std::memory_order_relaxed);
    }
    ~Jbig2ClassFactory() override { g_comObjects.fetch_sub(1, std::memory_order_acq_rel); }

    IFACEMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) override {
        if (pOuter) return CLASS_E_NOAGGREGATION;
        if (is_encoder_) return CreateJbig2BitmapEncoder(riid, ppv);
        auto decoder = Make<Jbig2BitmapDecoder>();
        return decoder ? decoder.CopyTo(riid, ppv) : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) g_comObjects.fetch_add(1, std::memory_order_relaxed);
        else       g_comObjects.fetch_sub(1, std::memory_order_acq_rel);
        return S_OK;
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hModule = hModule; DisableThreadLibraryCalls(hModule); }
    return TRUE;
}
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (rclsid == CLSID_Jbig2Decoder) {
        auto factory = Make<Jbig2ClassFactory>(false);
        return factory ? factory.CopyTo(riid, ppv) : E_OUTOFMEMORY;
    }
    if (rclsid == CLSID_Jbig2Encoder) {
        auto factory = Make<Jbig2ClassFactory>(true);
        return factory ? factory.CopyTo(riid, ppv) : E_OUTOFMEMORY;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
STDAPI DllCanUnloadNow() {
    return g_comObjects.load(std::memory_order_acquire) == 0 ? S_OK : S_FALSE;
}
STDAPI DllRegisterServer()   { return RegisterJbig2WicCodec(g_hModule); }
STDAPI DllUnregisterServer() { return UnregisterJbig2WicCodec(); }
// Direkte C-API fuer smart_dither.py (ctypes) -> 0 Temp-Dateien, 0 externe Tools
extern "C" int __stdcall jbig2_encode_1bpp(
    const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride,
    double dpi_x, double dpi_y, uint8_t** out_buf, size_t* out_size) {
    if (!pixels || !out_buf || !out_size || width == 0 || height == 0) return -1;
    *out_buf = nullptr; *out_size = 0;
    try {
        jbig2::Bitmap1bpp bmp;
        bmp.width = width; bmp.height = height; bmp.stride = stride;
        bmp.dpi_x = dpi_x; bmp.dpi_y = dpi_y;
        jbig2::Bitmap1bppView v{ pixels, static_cast<size_t>(stride) * height, width, height, stride, dpi_x, dpi_y }; auto encoded = jbig2::EncodeLosslessDocumentView(std::span<const jbig2::Bitmap1bppView>(&v, 1));
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

extern "C" void __stdcall jbig2_free_buffer(uint8_t* buf) {
    std::free(buf);
}
struct Jbig2PageInput {
    const uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    double dpi_x;
    double dpi_y;
};

extern "C" int __stdcall jbig2_encode_multipage_1bpp(
    const Jbig2PageInput* pages, uint32_t page_count, int enable_at_tuning,
    uint8_t** out_buf, size_t* out_size) {
    if (!pages || page_count == 0 || !out_buf || !out_size) return -1;
    *out_buf = nullptr; *out_size = 0;
    try {
        std::vector<jbig2::Bitmap1bpp> list;
        list.reserve(page_count);
        for (uint32_t i = 0; i < page_count; ++i) {
            if (!pages[i].pixels || pages[i].width == 0 || pages[i].height == 0) return -1;
            jbig2::Bitmap1bpp bmp;
            bmp.width = pages[i].width; bmp.height = pages[i].height; bmp.stride = pages[i].stride;
            bmp.dpi_x = pages[i].dpi_x; bmp.dpi_y = pages[i].dpi_y;
            bmp.pixels.assign(pages[i].pixels, pages[i].pixels + static_cast<size_t>(bmp.stride) * bmp.height);
            list.push_back(std::move(bmp));
        }
        jbig2::LosslessEncodeOptions opts;
        opts.adaptive_templates = (enable_at_tuning != 0);
        auto encoded = jbig2::EncodeLosslessDocument(list, opts);
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