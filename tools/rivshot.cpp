// rivshot.cpp — Headless Rive renderer: load a .riv, render a frame to a PNG.
//
//   rivshot <input.riv> <output.png> [size] [seconds]
//
// Uses a Direct2D WIC-bitmap render target (no GPU/window required), so it
// doubles as the validation harness for the Direct2D backend against a corpus
// of .riv files.
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

#include "../src/rive_scene.hpp"

using Microsoft::WRL::ComPtr;

static bool readFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

static HRESULT savePng(IWICImagingFactory* wic, IWICBitmap* bmp, const wchar_t* path) {
    ComPtr<IWICStream> stream;
    HRESULT hr = wic->CreateStream(&stream);
    if (FAILED(hr)) return hr;
    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapEncoder> enc;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (FAILED(hr)) return hr;
    hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = enc->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return hr;
    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return hr;

    UINT w = 0, h = 0;
    bmp->GetSize(&w, &h);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    hr = frame->WriteSource(bmp, nullptr);
    if (FAILED(hr)) return hr;
    hr = frame->Commit();
    if (FAILED(hr)) return hr;
    return enc->Commit();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: rivshot <input.riv> <output.png> [size] [seconds]\n");
        return 2;
    }
    const char* inPath = argv[1];
    const char* outPath = argv[2];
    int size = argc > 3 ? atoi(argv[3]) : 512;
    float seconds = argc > 4 ? (float)atof(argv[4]) : 0.0f;
    if (size < 16) size = 16;
    if (size > 4096) size = 4096;

    std::vector<uint8_t> bytes;
    if (!readFile(inPath, bytes)) {
        fprintf(stderr, "error: cannot read '%s'\n", inPath);
        return 1;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr);

    ComPtr<ID2D1Factory> d2d;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory),
                           reinterpret_cast<void**>(d2d.GetAddressOf()));
    if (FAILED(hr)) { fprintf(stderr, "D2D1CreateFactory failed 0x%08lx\n", hr); return 1; }

    ComPtr<IWICImagingFactory> wic;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&wic));
    if (FAILED(hr)) { fprintf(stderr, "WIC factory failed 0x%08lx\n", hr); return 1; }

    // Decide output dimensions: fit the artboard aspect inside size x size.
    rivepeek::RiveScene scene(d2d.Get(), wic.Get());
    if (!scene.load(bytes.data(), bytes.size())) {
        fprintf(stderr, "error: %s\n", scene.error().c_str());
        return 1;
    }
    scene.advance(seconds);

    float aw = scene.width(), ah = scene.height();
    int outW = size, outH = size;
    if (aw > 0 && ah > 0) {
        if (aw >= ah) { outW = size; outH = std::max(1, (int)(size * ah / aw)); }
        else { outH = size; outW = std::max(1, (int)(size * aw / ah)); }
    }

    ComPtr<IWICBitmap> target;
    hr = wic->CreateBitmap((UINT)outW, (UINT)outH, GUID_WICPixelFormat32bppPBGRA,
                           WICBitmapCacheOnLoad, &target);
    if (FAILED(hr)) { fprintf(stderr, "CreateBitmap failed 0x%08lx\n", hr); return 1; }

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> rt;
    hr = d2d->CreateWicBitmapRenderTarget(target.Get(), rtProps, &rt);
    if (FAILED(hr)) { fprintf(stderr, "CreateWicBitmapRenderTarget failed 0x%08lx\n", hr); return 1; }

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0.0f)); // transparent
    {
        rivepeek::D2DRenderer renderer(rt.Get(), d2d.Get());
        scene.draw(renderer, (float)outW, (float)outH);
    }
    hr = rt->EndDraw();
    if (FAILED(hr)) { fprintf(stderr, "EndDraw failed 0x%08lx\n", hr); return 1; }

    // Convert output path to wide.
    wchar_t wout[1024];
    MultiByteToWideChar(CP_UTF8, 0, outPath, -1, wout, 1024);
    hr = savePng(wic.Get(), target.Get(), wout);
    if (FAILED(hr)) { fprintf(stderr, "savePng failed 0x%08lx\n", hr); return 1; }

    printf("ok: %s -> %s (%dx%d, artboard %.0fx%.0f)\n", inPath, outPath, outW, outH, aw, ah);

    rt.Reset();
    target.Reset();
    wic.Reset();
    d2d.Reset();
    if (comInit) CoUninitialize();
    return 0;
}
