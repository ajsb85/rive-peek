// preview_handler.hpp — COM preview handler for .riv files.
//
// Implements the interfaces the Windows Shell needs to host a live preview in
// the Explorer preview pane (inside prevhost.exe):
//   IInitializeWithStream  — receive the file as a read-only IStream
//   IPreviewHandler        — lifecycle + rendering window (also IOleWindow)
//   IPreviewHandlerVisuals — background/text/font from the host theme
//   IObjectWithSite        — focus / accelerator plumbing
#pragma once
#define NOMINMAX
#include <windows.h>
#include <ole2.h>          // IOleWindow
#include <shobjidl.h>      // IPreviewHandler, IObjectWithSite, IInitializeWithStream
#include <thumbcache.h>    // IThumbnailProvider
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

#include "rive_scene.hpp"

namespace rivepeek {

using Microsoft::WRL::ComPtr;

extern HINSTANCE g_hInst;
void DllAddRef();
void DllRelease();

// Lightweight diagnostic log to %TEMP%\RivePeek.log. Always on so we can confirm
// whether the Shell actually loads and drives the handler inside prevhost.exe.
void Log(const char* fmt, ...);

class RivePreviewHandler : public IInitializeWithStream,
                           public IPreviewHandler,
                           public IOleWindow,
                           public IPreviewHandlerVisuals,
                           public IObjectWithSite,
                           public IThumbnailProvider {
public:
    RivePreviewHandler();
    virtual ~RivePreviewHandler();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD grfMode) override;

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND hwnd, const RECT* prc) override;
    IFACEMETHODIMP SetRect(const RECT* prc) override;
    IFACEMETHODIMP DoPreview() override;
    IFACEMETHODIMP Unload() override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP QueryFocus(HWND* phwnd) override;
    IFACEMETHODIMP TranslateAccelerator(MSG* pmsg) override;

    // IOleWindow (base of IPreviewHandler)
    IFACEMETHODIMP GetWindow(HWND* phwnd) override;
    IFACEMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    // IPreviewHandlerVisuals
    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override;
    IFACEMETHODIMP SetFont(const LOGFONTW* plf) override;
    IFACEMETHODIMP SetTextColor(COLORREF color) override;

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* site) override;
    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override;

    // IThumbnailProvider — static first-frame thumbnail for the file's grid icon.
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

public:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    void onPaint();
    void tick();
    bool ensureRenderTarget();
    void releaseDeviceResources();
    void createPreviewWindow();
    void destroyPreviewWindow();

    LONG m_ref = 1;

    // Shell-supplied state.
    HWND m_parent = nullptr;
    RECT m_rect = {0, 0, 0, 0};
    ComPtr<IUnknown> m_site;
    COLORREF m_bg = RGB(255, 255, 255);

    // File bytes copied out of the IStream (read-only snapshot).
    std::vector<uint8_t> m_data;

    // Rendering.
    HWND m_hwnd = nullptr;            // child window we draw into
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IWICImagingFactory> m_wic;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    std::unique_ptr<RiveScene> m_scene;
    UINT_PTR m_timer = 0;
    LARGE_INTEGER m_freq = {};
    LARGE_INTEGER m_last = {};
};

} // namespace rivepeek
