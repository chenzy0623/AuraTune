@echo off
setlocal
rem AuraTune build script - needs g++ (llvm-mingw / MSYS2 mingw64) on PATH
rem If your toolchain is elsewhere, edit the GXX line below.

set GXX=g++
if exist "D:\llvm-mingw\bin\g++.exe" set GXX=D:\llvm-mingw\bin\g++.exe

if not exist build mkdir build

echo [1/2] compiling sources with %GXX%
for %%F in (app audio audio_render audio_decode audio_capture audio_sessions dsp ui main) do (
    echo    %%F.cpp
    %GXX% -std=c++17 -O2 -c src\%%F.cpp -o build\%%F.o || goto :err
)

echo [2/2] linking
%GXX% -std=c++17 -O2 build\*.o -o build\AuraTune.exe -municode -mwindows ^
    -lmmdevapi -lmfplat -lmfreadwrite -lmfuuid -lole32 -lgdiplus -ldwmapi ^
    -lshell32 -luuid -lcomdlg32 -lshlwapi -lwinmm -loleaut32 || goto :err

echo.
echo build ok: build\AuraTune.exe
exit /b 0

:err
echo.
echo BUILD FAILED
exit /b 1
