@echo off
setlocal
rem Normal play or a manual local two-window test from the latest friend package.
set "PSModulePath="
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\friend\Start-Coop.ps1" %*
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo Finished with code %result%. Please read the messages above.
pause
exit /b %result%
