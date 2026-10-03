@echo off
setlocal
if not exist "obj" mkdir "obj"

set CFILES=code\main.cpp code\lexer.cpp code\parser.cpp code\types.cpp code\checker.cpp code\runtime.cpp code\interpreter.cpp code\ffi.cpp code\platform_win32.cpp
set CFLAGS=/O2 /GL /Gw /Ob3 /Fo"obj\\" /fp:fast /std:c++latest /nologo /c /EHa /MP /W4 /wd4100 /wd4189

cl %CFLAGS% %CFILES% || exit /b 1
ml64 /c /Fo"obj\\" /nologo code\abi_call.asm || exit /b 1
ml64 /c /Fo"obj\\" /nologo code\callback_router.asm || exit /b 1
ml64 /c /Fo"obj\\" /nologo code\trampolines.asm || exit /b 1

link obj\main.obj obj\lexer.obj obj\parser.obj obj\types.obj obj\checker.obj obj\runtime.obj obj\interpreter.obj obj\ffi.obj obj\platform_win32.obj obj\abi_call.obj obj\callback_router.obj obj\trampolines.obj /LTCG /OPT:REF /OPT:ICF /nologo /out:invoke.exe || exit /b 1

rem ABI/layout/callback torture DLL used by the examples and manual tests.
cl /nologo /O2 /LD code\test.c user32.lib gdi32.lib /Fe"examples\test.dll" /Fo"obj\test.obj" || exit /b 1
endlocal
