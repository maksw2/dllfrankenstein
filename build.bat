@echo off
cl /c /Zi /nologo caller.cpp || pause && exit /b
cl /c /Zi /nologo lexer.cpp || pause && exit /b
cl /c /Zi /nologo parser.cpp || pause && exit /b
cl /c /Zi /nologo handlers.cpp || pause && exit /b
ml64 /c /nologo helper.asm || pause && exit /b
link caller.obj lexer.obj parser.obj handlers.obj helper.obj /debug /nologo /out:invoke.exe || pause && exit /b
