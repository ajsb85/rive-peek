@echo off
REM Build RivePeek.dll - the COM preview handler - linking the D2D backend and
REM the prebuilt rive_core.lib into an in-process COM server.
setlocal ENABLEEXTENSIONS
set "ROOT=%~dp0.."
pushd "%ROOT%"
call "%ROOT%\build\env.bat" || (echo env setup failed & popd & exit /b 1)

set "RIVE=%ROOT%\rive-runtime"
set "OUT=%ROOT%\build\bin"
set "OBJ=%ROOT%\build\obj\dll"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

set "DEFS=/D_RIVE_INTERNAL_ /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /DNOMINMAX /D_WINDLL"
set "CFLAGS=/nologo /MT /O2 /EHsc /std:c++17 /GR- /wd4996 /wd4244 /wd4267 /I "%RIVE%\include" /I "%ROOT%\src""

cl %CFLAGS% %DEFS% /LD ^
   "%ROOT%\src\dllmain.cpp" "%ROOT%\src\preview_handler.cpp" ^
   "%ROOT%\src\rive_d2d.cpp" "%ROOT%\src\rive_scene.cpp" ^
   /Fe"%OUT%\RivePeek.dll" /Fo"%OBJ%\\" ^
   /link /DEF:"%ROOT%\src\rivepeek.def" /DLL ^
   "%ROOT%\build\rive_core.lib" ^
   d2d1.lib windowscodecs.lib ole32.lib oleaut32.lib shlwapi.lib shell32.lib advapi32.lib user32.lib gdi32.lib ^
   >"%ROOT%\build\dll_build.log" 2>&1

if errorlevel 1 (
    echo [build_dll] BUILD FAILED ^(see build\dll_build.log^):
    findstr /c:": error " /c:": fatal error " /c:"unresolved" "%ROOT%\build\dll_build.log"
    popd
    exit /b 1
)
echo [build_dll] OK  build\bin\RivePeek.dll
popd
exit /b 0
