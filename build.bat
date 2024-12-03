@echo off
REM Build script for steganography project

REM Create project directories
if not exist "build" mkdir build
if not exist "src" mkdir src
if not exist "include" mkdir include

REM Download STB header files if they don't exist
if not exist "include\stb_image.h" (
    echo Downloading stb_image.h...
    powershell -Command "Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h' -OutFile 'include\stb_image.h'"
)

if not exist "include\stb_image_write.h" (
    echo Downloading stb_image_write.h...
    powershell -Command "Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h' -OutFile 'include\stb_image_write.h'"
)

REM Set up compiler options
set CFLAGS=/W4 /wd4244 /wd4996 /nologo /I"include" /D_CRT_SECURE_NO_WARNINGS

REM Build the project
echo Building steganography...
cl.exe %CFLAGS% /Fo:"build\\" /Fe:"build\steganography.exe" src\steganography.c

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Executable created at build\steganography.exe
) else (
    echo Build failed!
)
