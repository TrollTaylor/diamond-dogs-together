@echo off
rem One-click offline co-op test: two MGSV copies side by side on this PC.
rem See tools\lab\Play-OfflineCoop.ps1 for details.
where pwsh >nul 2>nul
if errorlevel 1 goto nopwsh
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\lab\Play-OfflineCoop.ps1" %*
pause
exit /b

:nopwsh
echo PowerShell 7 (pwsh) is required. Install it from https://aka.ms/powershell
pause
exit /b 1
