@echo off
REM ===========================================================================
REM  RivePeek - MSVC x64 build environment
REM
REM  We configure the Visual C++ / Windows SDK toolchain manually instead of
REM  calling vcvars64.bat, because vcvars aborts when the optional "cmake"
REM  developer-command-prompt extension is missing (it is not needed here).
REM
REM  Usage:  call build\env.bat
REM ===========================================================================
setlocal DISABLEDELAYEDEXPANSION

REM --- Make sure the core Windows directories are reachable (some shells that
REM     spawn this script start with a stripped PATH). -------------------------
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%PATH%"

REM --- Locate Visual Studio via vswhere ---------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if not defined VSINSTALL (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC" set "VSINSTALL=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
)
if not defined VSINSTALL (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
)
if not defined VSINSTALL (
    echo [env.bat] ERROR: could not find a Visual Studio C++ toolset.
    exit /b 1
)

REM --- Pick the newest MSVC toolset under that install ------------------------
set "VCROOT="
for /f "delims=" %%v in ('dir /b /ad /on "%VSINSTALL%\VC\Tools\MSVC" 2^>nul') do set "VCVER=%%v"
set "VCROOT=%VSINSTALL%\VC\Tools\MSVC\%VCVER%"
if not exist "%VCROOT%\bin\Hostx64\x64\cl.exe" (
    echo [env.bat] ERROR: cl.exe not found under "%VCROOT%".
    exit /b 1
)

REM --- Pick the newest Windows 10/11 SDK --------------------------------------
set "WINSDK=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVER="
for /f "delims=" %%s in ('dir /b /ad /on "%WINSDK%\Include" 2^>nul') do (
    if exist "%WINSDK%\Include\%%s\um\windows.h" set "SDKVER=%%s"
)
if not defined SDKVER (
    echo [env.bat] ERROR: no usable Windows SDK found under "%WINSDK%\Include".
    exit /b 1
)

REM --- Compose INCLUDE / LIB / PATH -------------------------------------------
set "INCLUDE=%VCROOT%\include;%WINSDK%\Include\%SDKVER%\ucrt;%WINSDK%\Include\%SDKVER%\shared;%WINSDK%\Include\%SDKVER%\um;%WINSDK%\Include\%SDKVER%\winrt;%WINSDK%\Include\%SDKVER%\cppwinrt"
set "LIB=%VCROOT%\lib\x64;%WINSDK%\Lib\%SDKVER%\ucrt\x64;%WINSDK%\Lib\%SDKVER%\um\x64"
set "PATH=%VCROOT%\bin\Hostx64\x64;%WINSDK%\bin\%SDKVER%\x64;%PATH%"

echo [env.bat] MSVC    : %VCROOT%
echo [env.bat] Win SDK : %SDKVER%

REM --- Re-export the composed environment to the *caller* --------------------
endlocal & set "PATH=%PATH%" & set "INCLUDE=%INCLUDE%" & set "LIB=%LIB%"
exit /b 0
