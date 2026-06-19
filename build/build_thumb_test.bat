@echo off
REM Build the IThumbnailProvider test (tools\thumb_test.cpp).
setlocal ENABLEEXTENSIONS
set "ROOT=%~dp0.."
pushd "%ROOT%"
call "%ROOT%\build\env.bat" || (echo env setup failed & popd & exit /b 1)
set "OUT=%ROOT%\build\bin"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /MT /O2 /EHsc /std:c++17 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   "%ROOT%\tools\thumb_test.cpp" /Fe"%OUT%\thumb_test.exe" /Fo"%ROOT%\build\obj\\" ^
   >"%ROOT%\build\thumb_build.log" 2>&1
if errorlevel 1 (
    echo [build_thumb_test] BUILD FAILED:
    findstr /c:": error " /c:": fatal error " "%ROOT%\build\thumb_build.log"
    popd & exit /b 1
)
echo [build_thumb_test] OK  build\bin\thumb_test.exe
popd
exit /b 0
