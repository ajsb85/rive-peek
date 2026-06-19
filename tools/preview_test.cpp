// preview_test.cpp — In-process integration test for RivePeek.dll.
//
//   preview_test <input.riv> [output.png]
//
// Loads the DLL, walks the exact COM dance the Windows Shell performs
// (DllGetClassObject -> IClassFactory::CreateInstance -> IInitializeWithStream
// -> IPreviewHandler::SetWindow/DoPreview), pumps the message loop so the
// animation timer runs, optionally screenshots the preview window, then Unloads.
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>

#include "../src/guids.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

typedef HRESULT(STDAPICALLTYPE* PFN_DllGetClassObject)(REFCLSID, REFIID, void**);

static void pump(int ms) {
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
}

static bool savePng(HBITMAP hbmp, int w, int h, const wchar_t* path) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return false;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmapFromHBITMAP(hbmp, nullptr, WICBitmapUseAlpha, &bmp)))
        return false;
    ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&frame, &props);
    frame->Initialize(props.Get());
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    if (FAILED(frame->WriteSource(bmp.Get(), nullptr))) return false;
    frame->Commit();
    enc->Commit();
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: preview_test <input.riv> [out.png]\n"); return 2; }
    const wchar_t* inPath = argv[1];
    const wchar_t* outPath = argc > 2 ? argv[2] : nullptr;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE dll = LoadLibraryW(L"RivePeek.dll");
    if (!dll) { wprintf(L"FAIL: LoadLibrary RivePeek.dll (%lu)\n", GetLastError()); return 1; }
    auto getClass = (PFN_DllGetClassObject)GetProcAddress(dll, "DllGetClassObject");
    if (!getClass) { wprintf(L"FAIL: no DllGetClassObject\n"); return 1; }

    ComPtr<IClassFactory> factory;
    HRESULT hr = getClass(CLSID_RivePreviewHandler, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { wprintf(L"FAIL: DllGetClassObject 0x%08lx\n", hr); return 1; }
    wprintf(L"ok: class factory\n");

    ComPtr<IUnknown> unk;
    hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&unk));
    if (FAILED(hr)) { wprintf(L"FAIL: CreateInstance 0x%08lx\n", hr); return 1; }

    // Confirm every interface the Shell may ask for is reachable.
    ComPtr<IInitializeWithStream> init;
    ComPtr<IPreviewHandler> preview;
    ComPtr<IOleWindow> oleWin;
    ComPtr<IObjectWithSite> site;
    ComPtr<IPreviewHandlerVisuals> visuals;
    if (FAILED(unk.As(&init)))    { wprintf(L"FAIL: QI IInitializeWithStream\n"); return 1; }
    if (FAILED(unk.As(&preview))) { wprintf(L"FAIL: QI IPreviewHandler\n"); return 1; }
    if (FAILED(unk.As(&oleWin)))  { wprintf(L"FAIL: QI IOleWindow\n"); return 1; }
    if (FAILED(unk.As(&site)))    { wprintf(L"FAIL: QI IObjectWithSite\n"); return 1; }
    if (FAILED(unk.As(&visuals))) { wprintf(L"FAIL: QI IPreviewHandlerVisuals\n"); return 1; }
    wprintf(L"ok: all interfaces (Init, Preview, OleWindow, ObjectWithSite, Visuals)\n");

    // Stream the file just like the Shell does.
    ComPtr<IStream> stream;
    hr = SHCreateStreamOnFileEx(inPath, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE,
                                nullptr, &stream);
    if (FAILED(hr)) { wprintf(L"FAIL: open stream 0x%08lx\n", hr); return 1; }
    hr = init->Initialize(stream.Get(), STGM_READ);
    if (FAILED(hr)) { wprintf(L"FAIL: Initialize 0x%08lx\n", hr); return 1; }
    wprintf(L"ok: IInitializeWithStream::Initialize\n");

    visuals->SetBackgroundColor(RGB(245, 245, 245));

    // Host window standing in for the Explorer preview pane.
    const int W = 480, H = 480;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RivePeekTestHost";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND host = CreateWindowW(L"RivePeekTestHost", L"RivePeek Test", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, W + 40, H + 60, nullptr,
                              nullptr, wc.hInstance, nullptr);
    ShowWindow(host, outPath ? SW_SHOW : SW_HIDE);

    RECT rc = {10, 10, 10 + W, 10 + H};
    hr = preview->SetWindow(host, &rc);
    if (FAILED(hr)) { wprintf(L"FAIL: SetWindow 0x%08lx\n", hr); return 1; }
    hr = preview->DoPreview();
    if (FAILED(hr)) { wprintf(L"FAIL: DoPreview 0x%08lx\n", hr); return 1; }
    wprintf(L"ok: DoPreview\n");

    HWND child = nullptr;
    oleWin->GetWindow(&child);
    wprintf(L"ok: preview HWND = %p\n", (void*)child);

    pump(900); // let the animation timer run a few frames

    if (outPath && child) {
        HDC dc = GetDC(child);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
        HGDIOBJ old = SelectObject(mem, bmp);
        // PW_RENDERFULLCONTENT (0x2) captures D3D/D2D-backed windows.
        PrintWindow(child, mem, 0x2);
        SelectObject(mem, old);
        if (savePng(bmp, W, H, outPath)) wprintf(L"ok: screenshot -> %ls\n", outPath);
        else wprintf(L"warn: screenshot failed (render still verified via rivshot)\n");
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(child, dc);
    }

    preview->Unload();
    wprintf(L"ok: Unload\n");
    preview.Reset(); oleWin.Reset(); init.Reset(); site.Reset(); visuals.Reset();
    unk.Reset(); factory.Reset();
    DestroyWindow(host);

    wprintf(L"PASS: full COM preview-handler pipeline exercised in-process.\n");
    CoUninitialize();
    return 0;
}
