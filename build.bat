@echo off
rem ==== RTSurvivor: сборка через MinGW-w64 (C:\mingw64, gcc 16.2 UCRT) ====
rem Компилятор лежит вне проекта, т.к. GNU ld не переваривает
rem пробелы и кириллицу в собственном пути.
setlocal
pushd "%~dp0"

set "GPP=C:\mingw64\bin\g++.exe"
set "RAYLIB=third_party\raylib_mingw"

if not exist "%GPP%" (
    echo [build] MinGW not found: %GPP%
    exit /b 1
)
if not exist bin mkdir bin

"%GPP%" -std=c++20 -O2 -Wall -Wextra -mwindows -static ^
    -I "%RAYLIB%\include" ^
    "src\main.cpp" "src\sim.cpp" ^
    -o "bin\rts.exe" ^
    "%RAYLIB%\lib\libraylib.a" ^
    -static-libgcc -static-libstdc++ ^
    -lwinmm -lgdi32 -lopengl32 -luser32 -lshell32 ^
    2> "bin\build.log"

if errorlevel 1 (
    echo [build] FAILED, see bin\build.log
    type "bin\build.log"
    popd
    exit /b 1
)
popd
echo [build] OK: bin\rts.exe
