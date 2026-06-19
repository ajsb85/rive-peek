// thumb_test.cpp — Exercise RivePeek's IThumbnailProvider exactly as the Shell's
// thumbnail cache does, and save the produced HBITMAP to a PNG.
//
//   thumb_test <input.riv> <output.png> [cx]
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>

#include "../src/guids.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;

typedef HRESULT(STDAPICALLTYPE* PFN_DllGetClassObject)(REFCLSID, REFIID, void**);

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { wprintf(L"usage: thumb_test <input.riv> <output.png> [cx]\n"); return 2; }
    UINT cx = argc > 3 ? (UINT)_wtoi(argv[3]) : 256;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE dll = LoadLibraryW(L"RivePeek.dll");
    if (!dll) { wprintf(L"FAIL: LoadLibrary (%lu)\n", GetLastError()); return 1; }
    auto getClass = (PFN_DllGetClassObject)GetProcAddress(dll, "DllGetClassObject");

    ComPtr<IClassFactory> factory;
    if (FAILED(getClass(CLSID_RivePreviewHandler, IID_PPV_ARGS(&factory)))) { wprintf(L"FAIL: factory\n"); return 1; }
    ComPtr<IUnknown> unk;
    factory->CreateInstance(nullptr, IID_PPV_ARGS(&unk));

    ComPtr<IInitializeWithStream> init;
    ComPtr<IThumbnailProvider> thumb;
    if (FAILED(unk.As(&init)))  { wprintf(L"FAIL: QI IInitializeWithStream\n"); return 1; }
    if (FAILED(unk.As(&thumb))) { wprintf(L"FAIL: QI IThumbnailProvider\n"); return 1; }
    wprintf(L"ok: QI IThumbnailProvider\n");

    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(argv[1], STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream))) {
        wprintf(L"FAIL: open stream\n"); return 1;
    }
    if (FAILED(init->Initialize(stream.Get(), STGM_READ))) { wprintf(L"FAIL: Initialize\n"); return 1; }

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    HRESULT hr = thumb->GetThumbnail(cx, &hbmp, &alpha);
    if (FAILED(hr) || !hbmp) { wprintf(L"FAIL: GetThumbnail 0x%08lx\n", hr); return 1; }
    BITMAP bm = {};
    GetObject(hbmp, sizeof(bm), &bm);
    wprintf(L"ok: GetThumbnail -> %ldx%ld alpha=%d\n", bm.bmWidth, bm.bmHeight, (int)alpha);

    // Save HBITMAP to PNG.
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    ComPtr<IWICBitmap> wbmp;
    wic->CreateBitmapFromHBITMAP(hbmp, nullptr, WICBitmapUsePremultipliedAlpha, &wbmp);
    ComPtr<IWICStream> os; wic->CreateStream(&os);
    os->InitializeFromFilename(argv[2], GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> enc; wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(os.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&frame, &props); frame->Initialize(props.Get());
    frame->SetSize((UINT)bm.bmWidth, (UINT)bm.bmHeight);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA; frame->SetPixelFormat(&fmt);
    frame->WriteSource(wbmp.Get(), nullptr); frame->Commit(); enc->Commit();
    wprintf(L"ok: saved %ls\n", argv[2]);

    DeleteObject(hbmp);
    // Release all COM references BEFORE CoUninitialize.
    frame.Reset(); props.Reset(); enc.Reset(); os.Reset(); wbmp.Reset(); wic.Reset();
    thumb.Reset(); init.Reset(); unk.Reset(); factory.Reset(); stream.Reset();
    wprintf(L"PASS: IThumbnailProvider produced a thumbnail.\n");
    CoUninitialize();
    return 0;
}
