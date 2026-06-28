@echo off
rem One-command build for pingtrace. Double-click it, or run from any terminal.
rem Finds Visual Studio via vswhere, sets up the MSVC env, and builds with the
rem VS-bundled CMake + Ninja + vcpkg (no standalone tools needed on PATH).
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [build] vswhere not found - is Visual Studio installed?
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo [build] No Visual Studio installation found.
  exit /b 1
)

set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

cd /d "%~dp0" || exit /b 1
cmake --preset default || exit /b 1
cmake --build --preset default || exit /b 1

echo.
echo [build] OK -^> "%~dp0build\pingtrace.exe"
