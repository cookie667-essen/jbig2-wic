#include "wic_guids.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::wprintf(L"Usage: wic_roundtrip_test.exe <in.jb2> <out_multipage.jb2>\n"); return 1; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int rc = 0;
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            rc = 2;
        } else {
            ComPtr<IWICBitmapDecoder> dec;
            HRESULT hr = factory->CreateDecoderFromFilename(argv[1], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
            if (FAILED(hr)) { std::wprintf(L"Input-Decode hr=0x%08X\n", hr); rc = 3; }
            else {
                ComPtr<IWICBitmapFrameDecode> f0;
                dec->GetFrame(0, &f0);

                // Ãœber WIC-Encoder eine 2-Seiten-JBIG2 schreiben
                ComPtr<IWICStream> outStream;
                ComPtr<IWICBitmapEncoder> enc;
                factory->CreateStream(&outStream);
                outStream->InitializeFromFilename(argv[2], GENERIC_WRITE);
                hr = factory->CreateEncoder(GUID_ContainerFormatJbig2, nullptr, &enc);
                if (FAILED(hr)) { std::wprintf(L"CreateEncoder(JBIG2) hr=0x%08X\n", hr); rc = 4; }
                else {
                    enc->Initialize(outStream.Get(), WICBitmapEncoderNoCache);
                    for (int p = 0; p < 2; ++p) {
                        ComPtr<IWICBitmapFrameEncode> fe;
                        enc->CreateNewFrame(&fe, nullptr);
                        fe->Initialize(nullptr);
                        fe->WriteSource(f0.Get(), nullptr);
                        fe->Commit();
                    }
                    hr = enc->Commit();
                    if (FAILED(hr)) { std::wprintf(L"Encoder Commit hr=0x%08X\n", hr); rc = 5; }
                }
            }
            // ZurÃ¼cklesen per WIC-Decoder & Seitenzahl prÃ¼fen
            if (rc == 0) {
                ComPtr<IWICBitmapDecoder> dec2;
                HRESULT hr = factory->CreateDecoderFromFilename(argv[2], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec2);
                UINT count = 0;
                if (SUCCEEDED(hr)) dec2->GetFrameCount(&count);
                std::wprintf(L"WIC Multi-Page Roundtrip: hr=0x%08X | Seiten=%u\n", hr, count);
                if (FAILED(hr) || count != 2) rc = 6;
            }
        }
    }
    CoUninitialize();
    return rc;
}