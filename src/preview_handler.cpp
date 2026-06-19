// preview_handler.cpp
#include "preview_handler.hpp"
#include "guids.hpp"

#include <shlwapi.h>   // QISearch / QITAB
#include <d2d1helper.h>
#include <cstdarg>
#include <cstdio>
#include <new>

using namespace rivepeek;

namespace rivepeek {
void Log(const char* fmt, ...) {
    // Off unless RIVEPEEK_LOG is set in the environment (checked once).
    static int enabled = -1;
    if (enabled < 0) {
        char v[8];
        enabled = GetEnvironmentVariableA("RIVEPEEK_LOG", v, sizeof(v)) > 0 ? 1 : 0;
    }
    if (!enabled) return;

    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH) return;
    strcat_s(path, "RivePeek.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d pid=%lu] ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, GetCurrentProcessId());
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}
} // namespace rivepeek

namespace {
const wchar_t* kWndClass = L"RivePeekPreviewWindow";
const UINT kFrameMs = 33; // ~30 fps animation in the preview pane

bool registerWindowClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = RivePreviewHandler::WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint everything ourselves
    wc.lpszClassName = kWndClass;
    registered = (RegisterClassExW(&wc) != 0) ||
                 (GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    return registered;
}
} // namespace

namespace rivepeek {

RivePreviewHandler::RivePreviewHandler() {
    DllAddRef();
    QueryPerformanceFrequency(&m_freq);
    Log("RivePreviewHandler constructed");
}

RivePreviewHandler::~RivePreviewHandler() {
    destroyPreviewWindow();
    DllRelease();
}

// ----------------------------------------------------------------- IUnknown
IFACEMETHODIMP RivePreviewHandler::QueryInterface(REFIID riid, void** ppv) {
    // QISearch resolves IUnknown via the first table entry and performs the
    // correct pointer adjustment for each multiply-inherited interface.
    static const QITAB qit[] = {
        QITABENT(RivePreviewHandler, IInitializeWithStream),
        QITABENT(RivePreviewHandler, IPreviewHandler),
        QITABENT(RivePreviewHandler, IOleWindow),
        QITABENT(RivePreviewHandler, IObjectWithSite),
        QITABENT(RivePreviewHandler, IPreviewHandlerVisuals),
        QITABENT(RivePreviewHandler, IThumbnailProvider),
        {0},
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) RivePreviewHandler::AddRef() {
    return InterlockedIncrement(&m_ref);
}

IFACEMETHODIMP_(ULONG) RivePreviewHandler::Release() {
    LONG c = InterlockedDecrement(&m_ref);
    if (c == 0) delete this;
    return c;
}

// ------------------------------------------------- IInitializeWithStream
IFACEMETHODIMP RivePreviewHandler::Initialize(IStream* stream, DWORD) {
    if (!stream) return E_INVALIDARG;
    if (!m_data.empty()) return E_UNEXPECTED; // initialize once

    // Read the whole (read-only) stream into memory.
    STATSTG stat = {};
    ULONGLONG size = 0;
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))) {
        size = stat.cbSize.QuadPart;
    }
    if (size > 0 && size < (64ull << 20)) {
        m_data.resize((size_t)size);
    }
    // Stream from the start; read in chunks (Stat size is a hint, not a guarantee).
    LARGE_INTEGER zero = {};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    m_data.clear();
    uint8_t buf[64 * 1024];
    ULONG got = 0;
    while (SUCCEEDED(stream->Read(buf, sizeof(buf), &got)) && got > 0) {
        m_data.insert(m_data.end(), buf, buf + got);
        if (m_data.size() > (128ull << 20)) break; // sanity cap
        got = 0;
    }
    Log("Initialize: read %zu bytes (grfMode hint)", m_data.size());
    return m_data.empty() ? E_FAIL : S_OK;
}

// -------------------------------------------------------- IPreviewHandler
IFACEMETHODIMP RivePreviewHandler::SetWindow(HWND hwnd, const RECT* prc) {
    Log("SetWindow: hwnd=%p", (void*)hwnd);
    m_parent = hwnd;
    if (prc) m_rect = *prc;
    if (m_hwnd && m_parent) {
        SetParent(m_hwnd, m_parent);
        MoveWindow(m_hwnd, m_rect.left, m_rect.top, m_rect.right - m_rect.left,
                   m_rect.bottom - m_rect.top, TRUE);
    }
    return S_OK;
}

IFACEMETHODIMP RivePreviewHandler::SetRect(const RECT* prc) {
    if (prc) m_rect = *prc;
    if (m_hwnd) {
        MoveWindow(m_hwnd, m_rect.left, m_rect.top, m_rect.right - m_rect.left,
                   m_rect.bottom - m_rect.top, TRUE);
    }
    return S_OK;
}

IFACEMETHODIMP RivePreviewHandler::DoPreview() {
    Log("DoPreview: parent=%p rect=(%ld,%ld,%ld,%ld) dataBytes=%zu",
        (void*)m_parent, m_rect.left, m_rect.top, m_rect.right, m_rect.bottom,
        m_data.size());
    if (m_hwnd) return S_OK; // already previewing
    if (!m_parent) { Log("DoPreview: FAIL no parent"); return E_FAIL; }

    if (!m_d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory),
                                     reinterpret_cast<void**>(
                                         m_d2dFactory.GetAddressOf()))))
            return E_FAIL;
    }
    if (!m_wic) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&m_wic));
    }

    // Parse the file (best effort; an unparseable file still shows a blank pane).
    if (!m_scene && !m_data.empty()) {
        m_scene = std::make_unique<RiveScene>(m_d2dFactory.Get(), m_wic.Get());
        if (!m_scene->load(m_data.data(), m_data.size())) {
            Log("DoPreview: scene load FAILED: %s", m_scene->error().c_str());
            m_scene.reset(); // leave blank background on failure
        } else {
            Log("DoPreview: scene loaded (artboard %.0fx%.0f)", m_scene->width(),
                m_scene->height());
        }
    }

    createPreviewWindow();
    if (!m_hwnd) {
        Log("DoPreview: FAIL CreateWindow err=%lu", GetLastError());
        return E_FAIL;
    }
    Log("DoPreview: preview window created hwnd=%p", (void*)m_hwnd);

    QueryPerformanceCounter(&m_last);
    if (m_scene) {
        m_timer = SetTimer(m_hwnd, 1, kFrameMs, nullptr);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return S_OK;
}

IFACEMETHODIMP RivePreviewHandler::Unload() {
    destroyPreviewWindow();
    m_scene.reset();
    m_data.clear();
    m_data.shrink_to_fit();
    return S_OK;
}

IFACEMETHODIMP RivePreviewHandler::SetFocus() {
    if (m_hwnd) { ::SetFocus(m_hwnd); return S_OK; }
    return S_FALSE;
}

IFACEMETHODIMP RivePreviewHandler::QueryFocus(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = ::GetFocus();
    return *phwnd ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

IFACEMETHODIMP RivePreviewHandler::TranslateAccelerator(MSG* pmsg) {
    // Forward accelerators to the host frame, per the preview-handler contract.
    if (m_site) {
        ComPtr<IPreviewHandlerFrame> frame;
        if (SUCCEEDED(m_site.As(&frame)) && frame) {
            return frame->TranslateAccelerator(pmsg);
        }
    }
    return S_FALSE;
}

// -------------------------------------------------------------- IOleWindow
IFACEMETHODIMP RivePreviewHandler::GetWindow(HWND* phwnd) {
    if (!phwnd) return E_INVALIDARG;
    // Per Microsoft's preview-handler sample, return the host parent window.
    *phwnd = m_parent;
    return S_OK;
}
IFACEMETHODIMP RivePreviewHandler::ContextSensitiveHelp(BOOL) { return S_OK; }

// ------------------------------------------------- IPreviewHandlerVisuals
IFACEMETHODIMP RivePreviewHandler::SetBackgroundColor(COLORREF color) {
    m_bg = color;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    return S_OK;
}
IFACEMETHODIMP RivePreviewHandler::SetFont(const LOGFONTW*) { return S_OK; }
IFACEMETHODIMP RivePreviewHandler::SetTextColor(COLORREF) { return S_OK; }

// ----------------------------------------------------------- IObjectWithSite
IFACEMETHODIMP RivePreviewHandler::SetSite(IUnknown* site) {
    m_site = site;
    return S_OK;
}
IFACEMETHODIMP RivePreviewHandler::GetSite(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (m_site) return m_site.CopyTo(riid, ppv);
    return E_FAIL;
}

// ----------------------------------------------------- IThumbnailProvider
IFACEMETHODIMP RivePreviewHandler::GetThumbnail(UINT cx, HBITMAP* phbmp,
                                                WTS_ALPHATYPE* pdwAlpha) {
    if (!phbmp || !pdwAlpha) return E_POINTER;
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_ARGB;
    if (m_data.empty() || cx == 0) return E_FAIL;

    if (!m_d2dFactory &&
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory),
                                 reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()))))
        return E_FAIL;
    if (!m_wic &&
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic))))
        return E_FAIL;

    RiveScene scene(m_d2dFactory.Get(), m_wic.Get());
    if (!scene.load(m_data.data(), m_data.size())) {
        Log("GetThumbnail: scene load FAILED: %s", scene.error().c_str());
        return E_FAIL;
    }
    scene.advance(0.0f); // static first frame

    // Fit the artboard aspect inside cx x cx.
    UINT w = cx, h = cx;
    float aw = scene.width(), ah = scene.height();
    if (aw > 0 && ah > 0) {
        if (aw >= ah) { w = cx; h = std::max<UINT>(1, (UINT)(cx * ah / aw)); }
        else { h = cx; w = std::max<UINT>(1, (UINT)(cx * aw / ah)); }
    }

    ComPtr<IWICBitmap> wicBmp;
    if (FAILED(m_wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnLoad, &wicBmp)))
        return E_FAIL;

    auto rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(m_d2dFactory->CreateWicBitmapRenderTarget(wicBmp.Get(), rtProps, &rt)))
        return E_FAIL;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0.0f)); // transparent
    {
        D2DRenderer renderer(rt.Get(), m_d2dFactory.Get());
        scene.draw(renderer, (float)w, (float)h);
    }
    if (FAILED(rt->EndDraw())) return E_FAIL;

    // Copy into a top-down 32bpp DIB section (premultiplied BGRA -> WTSAT_ARGB).
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) return E_OUTOFMEMORY;

    if (FAILED(wicBmp->CopyPixels(nullptr, w * 4, w * 4 * h, (BYTE*)bits))) {
        DeleteObject(hbmp);
        return E_FAIL;
    }
    *phbmp = hbmp;
    *pdwAlpha = WTSAT_ARGB;
    Log("GetThumbnail: cx=%u -> %ux%u", cx, w, h);
    return S_OK;
}

// --------------------------------------------------------------- rendering
void RivePreviewHandler::createPreviewWindow() {
    if (!registerWindowClass()) return;
    int w = m_rect.right - m_rect.left;
    int h = m_rect.bottom - m_rect.top;
    m_hwnd = CreateWindowExW(
        0, kWndClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        m_rect.left, m_rect.top, w, h, m_parent, nullptr, g_hInst, this);
}

void RivePreviewHandler::destroyPreviewWindow() {
    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    releaseDeviceResources();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

void RivePreviewHandler::releaseDeviceResources() { m_rt.Reset(); }

bool RivePreviewHandler::ensureRenderTarget() {
    if (!m_hwnd || !m_d2dFactory) return false;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(std::max<LONG>(1, rc.right - rc.left),
                                   std::max<LONG>(1, rc.bottom - rc.top));
    if (m_rt) {
        D2D1_SIZE_U cur = m_rt->GetPixelSize();
        if (cur.width != size.width || cur.height != size.height) {
            m_rt->Resize(size);
        }
        return true;
    }
    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size), &m_rt);
    return SUCCEEDED(hr);
}

void RivePreviewHandler::onPaint() {
    if (!ensureRenderTarget()) { ValidateRect(m_hwnd, nullptr); return; }

    D2D1_SIZE_F sz = m_rt->GetSize();
    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(GetRValue(m_bg) / 255.0f, GetGValue(m_bg) / 255.0f,
                             GetBValue(m_bg) / 255.0f, 1.0f));
    if (m_scene) {
        D2DRenderer renderer(m_rt.Get(), m_d2dFactory.Get());
        m_scene->draw(renderer, sz.width, sz.height);
    }
    HRESULT hr = m_rt->EndDraw();
    static bool firstPaint = true;
    if (firstPaint) { Log("onPaint: first EndDraw hr=0x%08lx size=%.0fx%.0f scene=%d",
                          hr, sz.width, sz.height, m_scene ? 1 : 0); firstPaint = false; }
    if (hr == D2DERR_RECREATE_TARGET) {
        releaseDeviceResources(); // rebuilt on next paint
    }
    ValidateRect(m_hwnd, nullptr);
}

void RivePreviewHandler::tick() {
    if (!m_scene) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - m_last.QuadPart) / (float)m_freq.QuadPart;
    m_last = now;
    if (dt < 0 || dt > 1.0f) dt = 1.0f / 30.0f; // clamp pauses
    m_scene->advance(dt);
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK RivePreviewHandler::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<RivePreviewHandler*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            self->onPaint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            self->tick();
            return 0;
        case WM_SIZE:
            if (self->m_rt) self->ensureRenderTarget();
            return 0;
        case WM_ERASEBKGND:
            return 1; // avoid flicker; we clear in onPaint
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace rivepeek
