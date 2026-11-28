#include "wic_guids.h"
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <wincodec.h>
#include <string>

namespace {
constexpr wchar_t kVendorGuid[] = L"{F0E1D2C3-B4A5-4678-9012-3456789ABCDE}";
constexpr wchar_t kThumbEx[]    = L"{E357FCCD-A995-4576-B01F-234630154E96}";
constexpr wchar_t kPhotoThumb[] = L"{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}";

std::wstring GuidStr(REFGUID g) {
    wchar_t buf[64]{};
    StringFromGUID2(g, buf, 64);
    return buf;
}
bool SetStr(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& val) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                            static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
bool SetDword(HKEY root, const std::wstring& path, const wchar_t* name, DWORD val) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
bool SetBin(HKEY root, const std::wstring& path, const wchar_t* name, const BYTE* data, DWORD sz) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_BINARY, data, sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
} // namespace

HRESULT RegisterJbig2WicCodec(HMODULE hModule) {
    wchar_t dllPath[MAX_PATH]{};
    if (!GetModuleFileNameW(hModule, dllPath, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    const std::wstring decClsid = GuidStr(CLSID_Jbig2Decoder);
    const std::wstring encClsid = GuidStr(CLSID_Jbig2Encoder);
    const std::wstring fmtGuid  = GuidStr(GUID_ContainerFormatJbig2);
    const std::wstring pix1bpp  = GuidStr(GUID_WICPixelFormatBlackWhite);
    const std::wstring catDec   = GuidStr(CATID_WICBitmapDecoders);
    const std::wstring catEnc   = GuidStr(CATID_WICBitmapEncoders);

    bool ok = true;
    auto regCommon = [&](const std::wstring& clsid, const wchar_t* desc, const wchar_t* friendly) {
        const std::wstring base = L"CLSID\\" + clsid;
        ok &= SetStr(HKEY_CLASSES_ROOT, base, nullptr, desc);
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"FriendlyName", friendly);
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"Author", L"jbig2-tools");
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"Vendor", kVendorGuid);
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"Version", L"3.0.1.0");
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"SpecVersion", L"1.0.0.0");
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"ContainerFormat", fmtGuid);
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"FileExtensions", L".jb2,.jbig2");
        ok &= SetStr(HKEY_CLASSES_ROOT, base, L"MimeTypes", L"image/jbig2");
        ok &= SetDword(HKEY_CLASSES_ROOT, base, L"SupportLossless", 1);
        ok &= SetDword(HKEY_CLASSES_ROOT, base, L"SupportMultiframe", 1);
        ok &= SetStr(HKEY_CLASSES_ROOT, base + L"\\InprocServer32", nullptr, dllPath);
        ok &= SetStr(HKEY_CLASSES_ROOT, base + L"\\InprocServer32", L"ThreadingModel", L"Both");
        ok &= SetStr(HKEY_CLASSES_ROOT, base + L"\\Formats\\" + pix1bpp, nullptr, L"");
    };

    // 1. Decoder + Magic-Bytes-Pattern
    regCommon(decClsid, L"JBIG2 WIC Decoder", L"JBIG2 Decoder (PDFium)");
    static constexpr BYTE kPat[8]  = { 0x97, 0x4A, 0x42, 0x32, 0x0D, 0x0A, 0x1A, 0x0A };
    static constexpr BYTE kMask[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    const std::wstring patKey = L"CLSID\\" + decClsid + L"\\Patterns\\0";
    SetDword(HKEY_CLASSES_ROOT, patKey, L"Position", 0);
    SetDword(HKEY_CLASSES_ROOT, patKey, L"Length", 8);
    SetBin(HKEY_CLASSES_ROOT,   patKey, L"Pattern", kPat, 8);
    SetBin(HKEY_CLASSES_ROOT,   patKey, L"Mask", kMask, 8);
    SetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + catDec + L"\\Instance\\" + decClsid, L"CLSID", decClsid);
    SetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + catDec + L"\\Instance\\" + decClsid, L"FriendlyName", L"JBIG2 Decoder (PDFium)");

    // 2. Encoder
    regCommon(encClsid, L"JBIG2 WIC Encoder", L"JBIG2 Encoder (Lossless MQ)");
    SetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + catEnc + L"\\Instance\\" + encClsid, L"CLSID", encClsid);
    SetStr(HKEY_CLASSES_ROOT, L"CLSID\\" + catEnc + L"\\Instance\\" + encClsid, L"FriendlyName", L"JBIG2 Encoder (Lossless MQ)");

    // 3. Windows 11 Explorer-Thumbnails
    for (const wchar_t* ext : { L".jb2", L".jbig2" }) {
        SetStr(HKEY_CLASSES_ROOT, ext, L"PerceivedType", L"image");
        SetStr(HKEY_CLASSES_ROOT, ext, L"Content Type", L"image/jbig2");
        SetStr(HKEY_CLASSES_ROOT, std::wstring(ext) + L"\\ShellEx\\" + kThumbEx, nullptr, kPhotoThumb);
        SetStr(HKEY_CLASSES_ROOT, std::wstring(L"SystemFileAssociations\\") + ext + L"\\ShellEx\\" + kThumbEx, nullptr, kPhotoThumb);
    }

    if (!ok) return E_ACCESSDENIED;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

HRESULT UnregisterJbig2WicCodec() {
    const std::wstring decClsid = GuidStr(CLSID_Jbig2Decoder);
    const std::wstring encClsid = GuidStr(CLSID_Jbig2Encoder);
    const std::wstring catDec   = GuidStr(CATID_WICBitmapDecoders);
    const std::wstring catEnc   = GuidStr(CATID_WICBitmapEncoders);

    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + decClsid).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + catDec + L"\\Instance\\" + decClsid).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + encClsid).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + catEnc + L"\\Instance\\" + encClsid).c_str());

    for (const wchar_t* ext : { L".jb2", L".jbig2" }) {
        for (std::wstring keyPath : {
            std::wstring(ext) + L"\\ShellEx\\" + kThumbEx,
            std::wstring(L"SystemFileAssociations\\") + ext + L"\\ShellEx\\" + kThumbEx
        }) {
            wchar_t val[128]{}; DWORD sz = sizeof(val);
            if (RegGetValueW(HKEY_CLASSES_ROOT, keyPath.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, val, &sz) == ERROR_SUCCESS) {
                if (_wcsicmp(val, kPhotoThumb) == 0) RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath.c_str());
            }
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
