// rivepeek_ca.cpp — WiX custom action: broadcast a shell association change so
// Explorer picks up the new .riv preview/thumbnail handler immediately (no
// reboot / sign-out). Scheduled immediate, after InstallFinalize, so it runs in
// the installing user's context where SHChangeNotify is meaningful.
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <shlobj.h>

extern "C" __declspec(dllexport) UINT __stdcall NotifyShell(MSIHANDLE hInstall) {
    (void)hInstall;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ERROR_SUCCESS;
}
