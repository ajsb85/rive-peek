@echo off
REM Build the headless rivshot harness (tools\rivshot.cpp + the D2D backend),
REM linking against the prebuilt rive_core.lib.
setlocal ENABLEEXTENSIONS
set "ROOT=%~dp0.."
pushd "%ROOT%"
call "%ROOT%\build\env.bat" || (echo env setup failed & popd & exit /b 1)

set "RIVE=%ROOT%\rive-runtime"
set "OUT=%ROOT%\build\bin"
if not exist "%OUT%" mkdir "%OUT%"

set "DEFS=/D_RIVE_INTERNAL_ /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /DNOMINMAX"
set "CFLAGS=/nologo /MT /O2 /EHsc /std:c++17 /GR- /wd4996 /wd4244 /wd4267 /I "%RIVE%\include" /I "%ROOT%\src""

cl %CFLAGS% %DEFS% ^
   "%ROOT%\tools\rivshot.cpp" "%ROOT%\src\rive_d2d.cpp" "%ROOT%\src\rive_scene.cpp" ^
   /Fe"%OUT%\rivshot.exe" /Fo"%ROOT%\build\obj\\" ^
   /link "%ROOT%\build\rive_core.lib" d2d1.lib dxguid.lib windowscodecs.lib ole32.lib user32.lib ^
   >"%ROOT%\build\rivshot_build.log" 2>&1

if errorlevel 1 (
    echo [build_rivshot] BUILD FAILED ^(see build\rivshot_build.log^):
    findstr /c:": error " /c:": fatal error " "%ROOT%\build\rivshot_build.log"
    popd
    exit /b 1
)
echo [build_rivshot] OK  build\bin\rivshot.exe
popd
exit /b 0
