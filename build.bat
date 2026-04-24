@echo off
REM Build script for Multicore Load Balancing Scheduler
REM Requires Visual Studio or MinGW

echo ================================================
echo    Multicore Load Balancing Scheduler Build
echo ================================================

REM Check for Visual Studio (MSVC)
where cl.exe >nul 2>&1
if %errorlevel% == 0 (
    echo Using MSVC (Visual Studio)...
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    
    echo Compiling...
    cl /O2 /D_CRT_SECURE_NO_WARNINGS /W3 /Fe:scheduler.exe main.c src\kernel_detect.c src\scheduler.c src\ui.c /link kernel32.lib advapi32.lib
    
    if %errorlevel% == 0 (
        echo Build successful: scheduler.exe
    ) else (
        echo Build failed!
        exit /b 1
    )
    exit /b 0
)

REM Check for MinGW
where gcc.exe >nul 2>&1
if %errorlevel% == 0 (
    echo Using MinGW...
    
    echo Compiling...
    gcc -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -o scheduler.exe main.c src/kernel_detect.c src/scheduler.c src/ui.c -lkernel32 -ladvapi32
    
    if %errorlevel% == 0 (
        echo Build successful: scheduler.exe
    ) else (
        echo Build failed!
        exit /b 1
    )
    exit /b 0
)

REM Check for TDM-GCC
where tdm-gcc.exe >nul 2>&1
if %errorlevel% == 0 (
    echo Using TDM-GCC...
    
    echo Compiling...
    gcc -O2 -Wall -D_CRT_SECURE_NO_WARNINGS -o scheduler.exe main.c src/kernel_detect.c src/scheduler.c src/ui.c -lkernel32 -ladvapi32
    
    if %errorlevel% == 0 (
        echo Build successful: scheduler.exe
    ) else (
        echo Build failed!
        exit /b 1
    )
    exit /b 0
)

echo Error: No C compiler found!
echo Please install Visual Studio or MinGW-w64
exit /b 1