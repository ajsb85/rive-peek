@echo off
REM ===========================================================================
REM  Build rive_core.lib  -- the Rive C++ runtime compiled WITHOUT the optional
REM  text / layout / audio / scripting subsystems, so it needs no third-party
REM  dependencies (HarfBuzz, SheenBidi, Yoga, libpng...). Those subsystems are
REM  #ifdef-guarded in the runtime, so they compile to no-ops here.
REM ===========================================================================
setlocal ENABLEEXTENSIONS

set "ROOT=%~dp0.."
pushd "%ROOT%"
call "%ROOT%\build\env.bat" || (echo env setup failed & popd & exit /b 1)

set "RIVE=%ROOT%\rive-runtime"
set "OBJ=%ROOT%\build\obj\core"
if not exist "%OBJ%" mkdir "%OBJ%"

set "DEFS=/D_RIVE_INTERNAL_ /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS"
set "CFLAGS=/nologo /c /MP /MT /O2 /EHsc /std:c++17 /GR- /wd4146 /wd4996 /wd4244 /wd4267 /wd4018 /wd4305 /I "%RIVE%\include""

REM --- Collect every .cpp under the runtime src tree -------------------------
set "LIST=%ROOT%\build\_core_sources.txt"
del "%LIST%" 2>nul
for /r "%RIVE%\src" %%f in (*.cpp) do echo "%%f">>"%LIST%"

echo [build_rive_core] compiling runtime sources...
cl %CFLAGS% %DEFS% /Fo"%OBJ%\\" @"%LIST%" >"%ROOT%\build\core_compile.log" 2>&1
set "CL_ERR=%ERRORLEVEL%"

findstr /c:": error " "%ROOT%\build\core_compile.log" >nul 2>&1
if %ERRORLEVEL%==0 (
    echo [build_rive_core] COMPILE ERRORS ^(see build\core_compile.log^):
    findstr /c:": error " "%ROOT%\build\core_compile.log"
    popd
    exit /b 1
)

echo [build_rive_core] archiving rive_core.lib...
lib /nologo /OUT:"%ROOT%\build\rive_core.lib" "%OBJ%\*.obj" >"%ROOT%\build\core_lib.log" 2>&1
if errorlevel 1 (
    echo [build_rive_core] LIB FAILED ^(see build\core_lib.log^):
    type "%ROOT%\build\core_lib.log"
    popd
    exit /b 1
)

for %%S in ("%ROOT%\build\rive_core.lib") do echo [build_rive_core] OK  rive_core.lib = %%~zS bytes
popd
exit /b 0
