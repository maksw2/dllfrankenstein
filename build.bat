@echo off
set CFLAGS=/Ox /GL /Gw /Ob3 /Qpar /fp:fast /arch:AVX2 /std:c++latest /nologo

cl %CFLAGS% /c /EHa caller.cpp || pause && exit /b
cl %CFLAGS% /c lexer.cpp || pause && exit /b
cl %CFLAGS% /c parser.cpp || pause && exit /b
ml64 /c /nologo helper.asm || pause && exit /b

link caller.obj lexer.obj parser.obj helper.obj /LTCG /OPT:REF /OPT:ICF /nologo /out:invoke.exe || pause && exit /b
