@echo off
REM build.bat - compile kbackend.exe
REM Run from kcode-win/ root (or anywhere — paths absolute):
REM   backend\build.bat

setlocal
set ROOT=%~dp0..
set KCC=C:\Users\brian\Documents\GitHub\krypton\kcc.exe
set KHDR=C:\Users\brian\Documents\GitHub\krypton\headers

if not exist "%KCC%" (
    echo [build.bat] kcc.exe not found at %KCC%
    exit /b 1
)

echo [1/2] kcc -^> _kbackend.c
"%KCC%" --headers "%KHDR%" "%ROOT%\backend\main.k" > "%ROOT%\backend\_kbackend.c"
if errorlevel 1 ( echo kcc failed & exit /b 1 )

echo [2/2] gcc -^> kbackend.exe
gcc "%ROOT%\backend\_kbackend.c" -o "%ROOT%\kbackend.exe" -w
if errorlevel 1 ( echo gcc failed & exit /b 1 )

echo built %ROOT%\kbackend.exe
endlocal
