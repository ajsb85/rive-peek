@echo off
REM Build RivePeek-0.2.0-x64.msi (WiX v5).
REM   1. compile the SHChangeNotify custom-action DLL with MSVC
REM   2. wix build the dual-scope MSI (perUserOrPerMachine, WixUI_Advanced)
setlocal ENABLEEXTENSIONS
set "ROOT=%~dp0.."
call "%ROOT%\build\env.bat" || (echo env setup failed & exit /b 1)

set "WIX=%USERPROFILE%\.dotnet\tools\wix.exe"
if not exist "%WIX%" (echo wix not found at "%WIX%" & exit /b 1)
if not exist "%ROOT%\build\bin\RivePeek.dll" (echo build RivePeek.dll first ^(build\build_dll.bat^) & exit /b 1)
if not exist "%ROOT%\build\release" mkdir "%ROOT%\build\release"
if not exist "%ROOT%\build\obj" mkdir "%ROOT%\build\obj"

echo [build_msi] compiling custom action...
cl /nologo /LD /MT /O2 /EHsc /DNDEBUG ^
   "%ROOT%\installer\rivepeek_ca.cpp" ^
   /Fe"%ROOT%\installer\rivepeek_ca.dll" /Fo"%ROOT%\build\obj\\" ^
   /link msi.lib shell32.lib >"%ROOT%\build\ca_build.log" 2>&1
if errorlevel 1 (
    echo [build_msi] CUSTOM ACTION BUILD FAILED:
    findstr /c:": error " /c:": fatal error " "%ROOT%\build\ca_build.log"
    exit /b 1
)

echo [build_msi] building MSI...
pushd "%ROOT%\installer"
"%WIX%" build "RivePeek.wxs" ^
    -ext WixToolset.UI.wixext ^
    -arch x64 ^
    -d RiveDll="%ROOT%\build\bin\RivePeek.dll" ^
    -d CaDll="%ROOT%\installer\rivepeek_ca.dll" ^
    -o "%ROOT%\build\release\RivePeek-0.2.0-x64.msi"
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (echo [build_msi] WIX BUILD FAILED ^(rc=%RC%^) & exit /b 1)

echo [build_msi] OK  build\release\RivePeek-0.2.0-x64.msi
exit /b 0
