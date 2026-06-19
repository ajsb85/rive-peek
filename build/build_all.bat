@echo off
REM Build everything: the rive core lib, the COM handler DLL, and both test tools.
setlocal
set "ROOT=%~dp0.."
call "%ROOT%\build\build_rive_core.bat" || exit /b 1
call "%ROOT%\build\build_dll.bat"       || exit /b 1
call "%ROOT%\build\build_rivshot.bat"   || exit /b 1
call "%ROOT%\build\build_test.bat"      || exit /b 1
echo.
echo === RivePeek build complete ===
echo   build\bin\RivePeek.dll      (the preview handler - regsvr32 to install)
echo   build\bin\rivshot.exe       (headless .riv -^> .png renderer)
echo   build\bin\preview_test.exe  (in-process COM pipeline test)
exit /b 0
