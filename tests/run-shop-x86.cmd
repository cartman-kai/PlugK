@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [shop-tests] vswhere.exe not found. Install Visual Studio with the x86 C++ toolset and retry.
    exit /b 1
)
set "VSROOT="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
    echo [shop-tests] No Visual Studio installation with the x86 C++ toolset found.
    exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
if not exist "build\shop_tests\Win32" mkdir "build\shop_tests\Win32"
cl /nologo /W3 /WX /Od /utf-8 /TC /IplugK\inc /IplugK\deps\minhook\include tests\shop_optimization_test.c /Fobuild\shop_tests\Win32\shop_optimization_test.obj /Febuild\shop_tests\Win32\shop_optimization_test.exe /link user32.lib
if errorlevel 1 goto done
build\shop_tests\Win32\shop_optimization_test.exe
:done
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
