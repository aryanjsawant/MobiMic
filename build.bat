@echo off
rem Builds the plugin, runs the tests, and (if Inno Setup is installed) builds the installer.
setlocal
cd /d "%~dp0"
set VERSION=0.3.0

set "CMAKE=cmake"
where cmake >nul 2>nul || set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE%" -S plugin -B plugin\build -G "Visual Studio 17 2022" -A x64 || exit /b 1
"%CMAKE%" --build plugin\build --config Release --target MobiMic_VST3 MobiMicHeadlessTest MobiMicHostRecordTest -- -m -v:m -nologo || exit /b 1

python plugin\tests\run_tests.py || exit /b 1
python plugin\tests\test_ableton_helper.py || exit /b 1
python plugin\tests\test_host_record.py || exit /b 1
python plugin\tests\test_phone_page.py || exit /b 1

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo Inno Setup 6 not found - plugin built, installer skipped. Get it from https://jrsoftware.org/isdl.php
    exit /b 0
)
"%ISCC%" /Qp /DAppVersion=%VERSION% installer\setup.iss || exit /b 1
echo.
echo Installer: installer\Output\MobiMic-Setup-%VERSION%.exe
