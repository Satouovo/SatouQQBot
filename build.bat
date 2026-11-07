@echo off
setlocal
cd /d "%~dp0"
set "SATOU_CMAKE="
for %%I in (cmake.exe) do set "SATOU_CMAKE=%%~$PATH:I"
if defined SATOU_CMAKE goto cmake_found

set "SATOU_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%SATOU_VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%SATOU_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "SATOU_CMAKE=%%I"
)
if not defined SATOU_CMAKE (
  echo CMake was not found. Install CMake 3.20+ or the Visual Studio C++ CMake tools.
  exit /b 1
)

:cmake_found
for %%I in ("%SATOU_CMAKE%") do set "SATOU_CTEST=%%~dpIctest.exe"
"%SATOU_CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
"%SATOU_CMAKE%" --build build --config Release --parallel
if errorlevel 1 exit /b 1
"%SATOU_CTEST%" --test-dir build -C Release --output-on-failure
if errorlevel 1 exit /b 1
echo.
echo Built and tested: build\Release\SatouQQBot.exe
exit /b 0
