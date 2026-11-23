#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::wprintf(L"Usage: wic_smoke_test.exe <in.jb2> <out.png>\n"); return 1; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int rc = 0;
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            rc = 2;
        } else {
            ComPtr<IWICBitmapDecoder> decoder;
            HRESULT hr = factory->CreateDecoderFromFilename(argv[1], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
            if (FAILED(hr)) { std::wprintf(L"CreateDecoder hr=0x%08X\n", hr); rc = 3; }
            else {
                UINT frames = 0, w = 0, h = 0;
                decoder->GetFrameCount(&frames);
                ComPtr<IWICBitmapFrameDecode> frame;
                hr = decoder->GetFrame(0, &frame);
                if (FAILED(hr)) { std::wprintf(L"GetFrame hr=0x%08X\n", hr); rc = 4; }
                else {
                    frame->GetSize(&w, &h);
                    std::wprintf(L"Erfolg! Seiten: %u | Seite 0: %ux%u px\n", frames, w, h);
                    ComPtr<IWICStream> outStream;
                    ComPtr<IWICBitmapEncoder> pngEnc;
                    ComPtr<IWICBitmapFrameEncode> pngFrame;
                    factory->CreateStream(&outStream);
                    outStream->InitializeFromFilename(argv[2], GENERIC_WRITE);
                    factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pngEnc);
                    pngEnc->Initialize(outStream.Get(), WICBitmapEncoderNoCache);
                    pngEnc->CreateNewFrame(&pngFrame, nullptr);
                    pngFrame->Initialize(nullptr);
                    pngFrame->WriteSource(frame.Get(), nullptr);
                    pngFrame->Commit();
                    pngEnc->Commit();
                    std::wprintf(L"PNG geschrieben: %ls\n", argv[2]);
                }
            }
        }
    }
    CoUninitialize();
    return rc;
}