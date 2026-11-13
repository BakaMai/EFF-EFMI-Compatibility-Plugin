@echo off
setlocal enabledelayedexpansion

:: Supports either MSVC (cl.exe) or Zig/Clang/MinGW
where cl >nul 2>nul
if %errorlevel% equ 0 (
    echo [Build] Compiling with MSVC cl.exe ...
    if not exist plugin mkdir plugin
    cl /nologo /utf-8 /O2 /MT /LD ^
        /Ideps\minhook\include /Ideps\minhook\src ^
        src\hide_uid.c ^
        deps\minhook\src\buffer.c ^
        deps\minhook\src\hook.c ^
        deps\minhook\src\trampoline.c ^
        deps\minhook\src\hde\hde64.c ^
        user32.lib kernel32.lib ^
        /Fe"plugin\hide_uid.dll" /link /DLL
    del /q *.obj plugin\hide_uid.exp plugin\hide_uid.lib 2>nul
    echo [OK] Built plugin\hide_uid.dll
    exit /b 0
)

where zig >nul 2>nul
if %errorlevel% equ 0 (
    echo [Build] Compiling with zig cc ...
    if not exist plugin mkdir plugin
    zig cc -target x86_64-windows-gnu -shared -O2 -s -Wall -Wextra -Werror ^
        -Ideps\minhook\include -Ideps\minhook\src ^
        src\hide_uid.c ^
        deps\minhook\src\buffer.c ^
        deps\minhook\src\hook.c ^
        deps\minhook\src\trampoline.c ^
        deps\minhook\src\hde\hde64.c ^
        -luser32 -lkernel32 ^
        -o plugin\hide_uid.dll
    del /q plugin\hide_uid.lib plugin\hide_uid.pdb 2>nul
    echo [OK] Built plugin\hide_uid.dll
    exit /b 0
)

echo [ERROR] Neither cl.exe (MSVC) nor zig.exe found in PATH.
exit /b 1
