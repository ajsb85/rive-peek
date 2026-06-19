// dllmain.cpp — COM server plumbing: module lifetime, class factory, the four
// exported entry points, and (un)registration of the preview handler.
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>     // SHChangeNotify
#include <shlwapi.h>
#include <new>

#include "guids.hpp"
#include "preview_handler.hpp"

namespace rivepeek {
HINSTANCE g_hInst = nullptr;
static LONG g_cRefModule = 0; // outstanding objects + server locks
void DllAddRef() { InterlockedIncrement(&g_cRefModule); }
void DllRelease() { InterlockedDecrement(&g_cRefModule); }
} // namespace rivepeek

using namespace rivepeek;

// --------------------------------------------------------------- ClassFactory
class ClassFactory : public IClassFactory {
public:
    ClassFactory() { DllAddRef(); }
    virtual ~ClassFactory() { DllRelease(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* handler = new (std::nothrow) RivePreviewHandler();
        if (!handler) return E_OUTOFMEMORY;
        HRESULT hr = handler->QueryInterface(riid, ppv);
        handler->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) DllAddRef(); else DllRelease();
        return S_OK;
    }

private:
    LONG m_ref = 1;
};

// ------------------------------------------------------------------- exports
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    rivepeek::Log("DllGetClassObject called (match=%d)",
                  rclsid == CLSID_RivePreviewHandler ? 1 : 0);
    if (rclsid != CLSID_RivePreviewHandler) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return rivepeek::g_cRefModule == 0 ? S_OK : S_FALSE;
}

// --------------------------------------------------------------- registration
//
// Everything is registered under HKEY_CURRENT_USER\Software\Classes, exactly as
// Microsoft's RecipePreviewHandler sample does. Per-user registration needs no
// administrator rights, and the per-user approved-handlers list is honored by
// the Shell.
static HRESULT setString(const wchar_t* subkey, const wchar_t* name,
                         const wchar_t* value) {
    HKEY key;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             &key, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value),
                       (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(r);
}

struct RegEntry { const wchar_t* subkey; const wchar_t* name; const wchar_t* data; };

STDAPI DllRegisterServer() {
    wchar_t module[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, module, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    const RegEntry entries[] = {
        // COM class.
        {L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, nullptr, RIVEPEEK_FRIENDLY_NAME},
        {L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, L"AppID", RIVEPEEK_SURROGATE_APPID_STR},
        {L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, L"DisplayName", RIVEPEEK_FRIENDLY_NAME},
        {L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR L"\\InprocServer32", nullptr, module},
        {L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR L"\\InprocServer32", L"ThreadingModel", L"Apartment"},
        // Associate the .riv extension with the preview-handler shell extension.
        // NOTE: deliberately NO PerceivedType — setting "image" makes the Shell
        // route .riv to the built-in image thumbnail/preview handlers (which
        // can't decode a .riv and show a generic picture icon) instead of ours.
        {L"Software\\Classes\\.riv", nullptr, L"RivePeek.Document"},
        {L"Software\\Classes\\.riv\\ShellEx\\" SHELLEX_PREVIEWHANDLER_STR, nullptr, RIVEPEEK_CLSID_STR},
        {L"Software\\Classes\\.riv\\ShellEx\\" SHELLEX_THUMBNAILPROVIDER_STR, nullptr, RIVEPEEK_CLSID_STR},
        {L"Software\\Classes\\RivePeek.Document", nullptr, L"Rive Animation"},
        {L"Software\\Classes\\RivePeek.Document\\ShellEx\\" SHELLEX_PREVIEWHANDLER_STR, nullptr, RIVEPEEK_CLSID_STR},
        {L"Software\\Classes\\RivePeek.Document\\ShellEx\\" SHELLEX_THUMBNAILPROVIDER_STR, nullptr, RIVEPEEK_CLSID_STR},
        // Approved preview-handlers list (the Shell's gatekeeper).
        {L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers", RIVEPEEK_CLSID_STR, RIVEPEEK_FRIENDLY_NAME},
    };

    HRESULT hr = S_OK;
    for (const auto& e : entries) {
        hr = setString(e.subkey, e.name, e.data);
        if (FAILED(hr)) return hr;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR);
    // Remove our per-user .riv class entirely (default ProgID, ShellEx, and any
    // PerceivedType written by older builds).
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.riv");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\RivePeek.Document");

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
                      0, KEY_WRITE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, RIVEPEEK_CLSID_STR);
        RegCloseKey(key);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// ------------------------------------------------------------------- DllMain
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        rivepeek::g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}
