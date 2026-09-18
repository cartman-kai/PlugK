@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
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
