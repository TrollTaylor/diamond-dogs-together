@echo off
setlocal
rem Developer only (not shipped to players): the local two-window test with two
rem MGSV copies on one PC, tools\dev\Start-LocalCoop.ps1. Players use
rem DiamondDogsTogether.exe (Steam online). Arguments pass through, for example
rem -GameDirectory, -GuestDirectory and -PackageDirectory.
set "PSModulePath="
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\dev\Start-LocalCoop.ps1" %*
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo Finished with code %result%. Please read the messages above.
pause
exit /b %result%
