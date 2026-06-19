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
static LONG setString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                      const wchar_t* value) {
    HKEY key;
    LONG r = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr,
                             &key, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value),
                       (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r;
}

STDAPI DllRegisterServer() {
    wchar_t module[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, module, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    const HKEY root = HKEY_LOCAL_MACHINE;

    // 1) COM class.
    setString(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, nullptr,
              RIVEPEEK_FRIENDLY_NAME);
    setString(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, L"AppID",
              RIVEPEEK_SURROGATE_APPID_STR);
    setString(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR, L"DisplayName",
              RIVEPEEK_FRIENDLY_NAME);
    setString(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR
              L"\\InprocServer32", nullptr, module);
    setString(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR
              L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    // 2) Associate the .riv extension with the preview-handler shell extension.
    setString(root, L"Software\\Classes\\.riv", nullptr, L"RivePeek.Document");
    setString(root, L"Software\\Classes\\.riv", L"PerceivedType", L"image");
    setString(root, L"Software\\Classes\\.riv\\ShellEx\\"
              SHELLEX_PREVIEWHANDLER_STR, nullptr, RIVEPEEK_CLSID_STR);
    setString(root, L"Software\\Classes\\RivePeek.Document", nullptr,
              L"Rive Animation");
    setString(root, L"Software\\Classes\\RivePeek.Document\\ShellEx\\"
              SHELLEX_PREVIEWHANDLER_STR, nullptr, RIVEPEEK_CLSID_STR);

    // 3) Add to the approved preview-handlers list (gatekeeper).
    setString(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
              RIVEPEEK_CLSID_STR, RIVEPEEK_FRIENDLY_NAME);

    // Tell the shell that file associations changed.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    const HKEY root = HKEY_LOCAL_MACHINE;
    RegDeleteTreeW(root, L"Software\\Classes\\CLSID\\" RIVEPEEK_CLSID_STR);
    RegDeleteTreeW(root, L"Software\\Classes\\.riv\\ShellEx\\"
                   SHELLEX_PREVIEWHANDLER_STR);
    RegDeleteTreeW(root, L"Software\\Classes\\RivePeek.Document");

    HKEY key;
    if (RegOpenKeyExW(root,
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
