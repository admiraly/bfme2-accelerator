@echo off
setlocal
rem Locate the x86 compiler on both hosted runners and developer machines.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ACCEL_VS=%%i"
if not defined ACCEL_VS exit /b 2
call "%ACCEL_VS%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 2
cd /d "%~dp0..\src"
cl /nologo /O2 /MT /W3 /I..\vendor\rpmalloc /c ..\vendor\rpmalloc\rpmalloc.c
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /LD /I..\vendor\rpmalloc /Fe:aotr_accel.dll aotr_accel.cpp rpmalloc.obj /link kernel32.lib user32.lib advapi32.lib /MAP
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /DAOTR_PROD /LD /I..\vendor\rpmalloc /Fe:bfme2_accel.dll aotr_accel.cpp rpmalloc.obj /link kernel32.lib user32.lib advapi32.lib /MAP
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /Fe:inject.exe inject.cpp /link kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /Fe:testload.exe testload.cpp /link kernel32.lib
if errorlevel 1 exit /b 1
call prepare_art.bat
if errorlevel 1 exit /b 1
rc /nologo /fo launcher.res launcher.rc
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /Fe:bfme2_accel_loader.exe launcher.cpp launcher.res /link kernel32.lib user32.lib advapi32.lib gdi32.lib gdiplus.lib ole32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /MANIFEST:NO
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /EHa /Fe:audiolimit_test.exe audiolimit_test.cpp /link kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /EHa /Fe:logicslicer_test.exe logicslicer_test.cpp /link kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /Fe:crt_test2.exe crt_test2.cpp /link kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /EHsc /Fe:rlsort_test.exe rlsort_test.cpp /link kernel32.lib /BASE:0x70000000 /FIXED /DYNAMICBASE:NO
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /W3 /Fe:rt_harness.exe rt_harness.cpp /link kernel32.lib user32.lib
if errorlevel 1 exit /b 1
exit /b 0
