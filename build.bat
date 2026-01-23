@echo off
if not exist "obj" mkdir "obj"
set CFLAGS=/Ox /GL /Gw /Ob3 /Qpar /Fo"obj\\" /fp:fast /arch:AVX2 /std:c++latest /nologo /c /EHa /MP

cl %CFLAGS% code\caller.cpp code\lexer.cpp code\parser.cpp || pause && exit /b
ml64 /c /Fo"obj\\" /nologo code\helper.asm || pause && exit /b

link obj\caller.obj obj\lexer.obj obj\parser.obj obj\helper.obj /LTCG /OPT:REF /OPT:ICF /nologo /out:invoke.exe || pause && exit /b
