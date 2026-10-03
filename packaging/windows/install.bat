@echo off
rem Installs Bounce.vst3 into the standard VST3 folder that FL Studio scans.
rem Right-click this file and choose "Run as administrator".

setlocal
set "SRC=%~dp0Bounce.vst3"
set "DEST=%CommonProgramFiles%\VST3"

if not exist "%SRC%" (
    echo Bounce.vst3 was not found next to this script.
    pause
    exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
    echo Please right-click install.bat and choose "Run as administrator".
    pause
    exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%"
if exist "%DEST%\Bounce.vst3" rmdir /s /q "%DEST%\Bounce.vst3"
xcopy "%SRC%" "%DEST%\Bounce.vst3\" /e /i /q /y >nul
if errorlevel 1 (
    echo Copy failed. Is FL Studio open? Close it and try again.
    pause
    exit /b 1
)

echo.
echo Installed to "%DEST%\Bounce.vst3"
echo In FL Studio: Options ^> Manage plugins ^> Find installed plugins, then add Bounce
echo from the plugin database (Generators).
echo.
pause
