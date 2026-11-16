@echo off
setlocal
title CivSwitch Cardputer - PC Switch Emulator
cd /d "%~dp0"
echo ======================================================
echo CivSwitch PC TEST EMULATOR - TCP port 9026
echo ======================================================
echo.
echo This runs a SAFE fake endpoint to test Cardputer Wi-Fi
echo and TCP connectivity. It is NOT a Switch 2 jailbreak.
echo.
echo Steps:
echo   1. Connect your PC and Cardputer to the same Wi-Fi.
echo   2. Allow PowerShell access through Windows Firewall
echo      on PRIVATE networks if prompted.
echo   3. On Cardputer: A = Setup, select Set Switch / PC IP.
echo   4. Enter your PC's IPv4 address shown below.
echo   5. On Cardputer: C = Test. Expect PC EMULATOR.
echo.
if not exist "%~dp0tools\switch_emulator.ps1" (
  echo ERROR: Missing tools\switch_emulator.ps1
  echo Download or clone the entire repository.
  pause
  exit /b 1
)
where powershell.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: Windows PowerShell is required.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\switch_emulator.ps1" -Port 9026
echo.
echo Emulator stopped. Press any key to close.
pause >nul
