# dllfrankenstein – call any DLL function anytime anywhere

dllfrankenstein lets you call arbitrary DLL functions with explicit type control, direct memory access, and a REPL.  
scary shit

## warning

This tool is unsafe by design.
Lying about signatures will most likely crash the process or corrupt the stack.

## how 2 use it

to build the app:
- `cl /c /Zi caller.c`
- `ml64 /c helper.asm`
- `link caller.obj helper.obj /debug /out:caller.exe`

to build the test dll:
- `cl /LD /Zi test.c /link /debug user32.lib gdi32.lib legacy_stdio_definitions.lib`

## help string

```
C:\Users\Administrator\Documents\dllfrankenstein>caller
wilczurski's cool shit - repl + ffi
Usage: caller.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...) [--print-result] [--assert=<type>]
    <func_name> can be an ordinal #<ordinal>
    or: caller.exe --interactive [--normal-variables-pretty-please] or caller.exe --script <script_path>
    Scripts by default use .ffi
    <type> can be: zero, nonzero, negative, nonnegative, not specifying means none
Usage in interactive mode:
    To write a comment use ; like assembly
    /loaddll <path>                     Load and focus a DLL
    /freedll <path>                     Unload a DLL from registry
    /alloc   <size>                     Allocate memory. Provide <size> in bytes
    /free    <addr>                     Free allocated memory
    /set     <addr>     <type>  <value> Store a value at a memory address
    /memset  <addr>     <value> [count] Set a block of memory to a byte value
    /get     <addr>     <type>          Get a value from a memory address
    /hex     <addr>     [count]         Hex dump memory (default 64 bytes)
    /address <dll_path> <name>          Get a function pointer by name
    /dlls                               List loaded DLLs
    /for     <count>    {<cmd>}...      Repeat {commands} <count> times
    /repeat-until       {<cmd>}...      Repeat {commands} until assert
    /quit                               Exit the program
Variables by default are Write-Once Read-Many, no shadowing, no scopes.
    --normal-variables-pretty-please allows reassignment. Not recommended.
    $<name> = <type> <value>            Assign a variable
    $<name> = rhs                       Function call or command
    Variables can be used as function arguments, like test.dll void print(str "%d", $var1)
    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'
Usage in interactive mode is the same as non-interactive except when focused on a DLL,
then you don't need to specify <dll_path>
Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void
    Equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t,
    float, double, null-terminated string (char*), wide string (wchar_t*), pointer (always hex), and void
You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type.
```

## examples

<details>
<summary><b>a simple messagebox</b></summary>

```
caller.exe user32.dll i32 MessageBoxA(voidptr 0, str "hi", str "title", u32 0)
caller.exe user32.dll i32 MessageBoxW(voidptr 0, wstr "hi", wstr "title", u32 0)
```

</details>

<details>
<summary><b>creating a win32 window</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> $lpfnWndProc = /address user32.dll DefWindowProcA
0x7ffcde461870
$lpfnWndProc = 0x7ffcde461870
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result --assert=nonzero
Result: 0x7ff7854b0000
$hInstance = 0x7ff7854b0000
> $lpszClassName = /alloc 14
0x0000024BF4B35180
$lpszClassName = 0x24bf4b35180
> /memset $lpszClassName 0 14
Set 14 bytes at 0x0000024BF4B35180 to 0x00
> /set $lpszClassName str "mywindowclass"
Value at 0x0000024BF4B35180 (str): mywindowclass
> $wc = /alloc 72
0x0000024BF4B21B20
$wc = 0x24bf4b21b20
> /memset $wc 0 72
Set 72 bytes at 0x0000024BF4B21B20 to 0x00
> /set $wc+0x08 voidptr $lpfnWndProc
Value at 0x0000024BF4B21B28 (voidptr): 0x7ffcde461870
> /set $wc+0x18 voidptr $hInstance
Value at 0x0000024BF4B21B38 (voidptr): 0x7ff7854b0000
> /set $wc+0x40 voidptr $lpszClassName
Value at 0x0000024BF4B21B60 (voidptr): 0x24bf4b35180
> $class_atom = user32.dll u16 RegisterClassA(voidptr $wc) --print-result --assert=nonzero
Result: 50150
$class_atom = 0xc3e6
> $hWnd = user32.dll voidptr CreateWindowExA(u32 0, voidptr $lpszClassName, str "mywindowname", u32 0x00CF0000, i32 100, i32 100, i32 100, i32 100, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0x7d0f02
$hWnd = 0x7d0f02
> user32.dll i32 ShowWindow(voidptr $hWnd, i32 5)
```

</details>

<details>
<summary><b>running a script; creating a win32 window</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --script window.ffi
wilczurski's cool shit - script
> $lpfnWndProc = /address test.dll WindowProcess
[Auto-Registered: test.dll]
0x7FFC97A01EE2
$lpfnWndProc = 0x7FFC97A01EE2
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result --assert=nonzero
Result: 0x7FF7E2150000
$hInstance = 0x7FF7E2150000
> $lpszClassName = /alloc 14
0x21145208700
$lpszClassName = 0x21145208700
> /memset $lpszClassName 0 14
Set 14 bytes at 0x21145208700 to 0x00
> /set $lpszClassName str "mywindowclass"
Value at 0x21145208700 (str): mywindowclass
> $hCursor = user32.dll voidptr LoadCursorA(voidptr 0, i64 32512)
$hCursor = 0x10003
> $wc = /alloc 72
0x211452077A0
$wc = 0x211452077A0
> /memset $wc 0 72
Set 72 bytes at 0x211452077A0 to 0x00
> /set $wc u32 3 ; style
Value at 0x211452077A0 (u32): 3
> /set $wc+0x08 voidptr $lpfnWndProc ; lpfnWndProc
Value at 0x211452077A8 (voidptr): 0x7FFC97A01EE2
> ; i32 0x10 cbClsExtra
> ; i32 0x14 cbWndExtra
> /set $wc+0x18 voidptr $hInstance ; hInstance
Value at 0x211452077B8 (voidptr): 0x7FF7E2150000
> /set $wc+0x28 voidptr $hCursor
Value at 0x211452077C8 (voidptr): 0x10003
> ; voidptr 0x20 hIcon
> /set $wc+0x30 voidptr 6 ; hbrBackground
Value at 0x211452077D0 (voidptr): 0x6
> ; str 0x38 lpszMenuName
> /set $wc+0x40 voidptr $lpszClassName ; lpszClassName
Value at 0x211452077E0 (voidptr): 0x21145208700
> $class_atom = user32.dll u16 RegisterClassA(voidptr $wc) --print-result --assert=nonzero
Result: 49964
$class_atom = 0xC32C
> $hWnd = user32.dll voidptr CreateWindowExA(u32 0, voidptr $lpszClassName, str "mywindowname", u32 0x00CF0000, i32 100, i32 100, i32 400, i32 200, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0x2E0924
$hWnd = 0x2E0924
> user32.dll i32 ShowWindow(voidptr $hWnd, i32 5)
> $hdc = user32.dll voidptr GetDC(voidptr $hWnd) --assert=nonzero
$hdc = 0x21011316
> $hPen = gdi32.dll voidptr CreatePen(i32 0, i32 5, u32 0x00FF0000) --assert=nonzero
$hPen = 0xFFFFFFFFE13008EB
> $hOldPen = gdi32.dll voidptr SelectObject(voidptr $hdc, voidptr $hPen)
$hOldPen = 0xB00017
> gdi32.dll i32 MoveToEx(voidptr $hdc, i32 50, i32 20, voidptr 0)
> gdi32.dll i32 LineTo(voidptr $hdc, i32 20, i32 80)
> gdi32.dll i32 LineTo(voidptr $hdc, i32 80, i32 80)
> gdi32.dll i32 LineTo(voidptr $hdc, i32 50, i32 20)
> gdi32.dll voidptr SelectObject(voidptr $hdc, voidptr $hOldPen)
> gdi32.dll i32 DeleteObject(voidptr $hPen)
> user32.dll i32 ReleaseDC(voidptr $hWnd, voidptr $hdc)
> $msg = /alloc 48
0x2114522AF00
$msg = 0x2114522AF00
> /memset $msg 0 48
Set 48 bytes at 0x2114522AF00 to 0x00
> /repeat-until {user32.dll i32 GetMessageA(voidptr $msg, voidptr 0, u32 0, u32 0) --assert=nonzero}{user32.dll i32 TranslateMessage(voidptr $msg)}{user32.dll i64 DispatchMessageA(voidptr $msg)}{kernel32.dll void Sleep(i32 100)}
Assertion failed for result: 0
> msvcrt.dll i32 printf(str "goodbye!\n")
goodbye!
> /free $lpszClassName
Freed memory at 0x21145208700
> /quit
```

</details>

<details>
<summary><b>running a script; creating a win32 window using wide strings</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --script window_w.ffi
wilczurski's cool shit - script
> ; window_w.ffi
> $lpfnWndProc = /address test.dll WindowProcessW
[Auto-Registered: test.dll]
0x7FFAEE184B6F
$lpfnWndProc = 0x7FFAEE184B6F
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result --assert=nonzero
Result: 0x7FF6422E0000
$hInstance = 0x7FF6422E0000
> $lpszClassName = wstr "mywindowclass"
$lpszClassName = 0x21CBE667540
> $lpWindowName = wstr "mywindowname"
$lpWindowName = 0x21CBE667450
> $hCursor = user32.dll voidptr LoadCursorW(voidptr 0, i64 32512)
$hCursor = 0x10003
> $wc = /alloc 72 ; WNDCLASSW
0x21CBE6638B0
$wc = 0x21CBE6638B0
> /memset $wc 0 72
Set 72 bytes at 0x21CBE6638B0 to 0x00
> /set $wc u32 3 ; style
Value at 0x21CBE6638B0 (u32): 3
> /set $wc+0x08 voidptr $lpfnWndProc ; lpfnWndProc
Value at 0x21CBE6638B8 (voidptr): 0x7FFAEE184B6F
> ; i32 0x10 cbClsExtra
> ; i32 0x14 cbWndExtra
> /set $wc+0x18 voidptr $hInstance ; hInstance
Value at 0x21CBE6638C8 (voidptr): 0x7FF6422E0000
> /set $wc+0x28 voidptr $hCursor
Value at 0x21CBE6638D8 (voidptr): 0x10003
> ; voidptr 0x20 hIcon
> /set $wc+0x30 voidptr 6 ; hbrBackground
Value at 0x21CBE6638E0 (voidptr): 0x6
> ; str 0x38 lpszMenuName
> /set $wc+0x40 voidptr $lpszClassName ; lpszClassName
Value at 0x21CBE6638F0 (voidptr): 0x21CBE667540
> $class_atom = user32.dll u16 RegisterClassW(voidptr $wc) --print-result --assert=nonzero
Result: 49473
$class_atom = 0xC141
> $hWnd = user32.dll voidptr CreateWindowExW(u32 0, voidptr $lpszClassName, voidptr $lpWindowName, u32 0x00CF0000, i32 100, i32 100, i32 400, i32 200, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0xB0B60
$hWnd = 0xB0B60
> user32.dll i32 ShowWindow(voidptr $hWnd, i32 5)
> $msg = /alloc 48
0x21CBE6890A0
$msg = 0x21CBE6890A0
> /memset $msg 0 48
Set 48 bytes at 0x21CBE6890A0 to 0x00
> /repeat-until {user32.dll i32 GetMessageW(voidptr $msg, voidptr 0, u32 0, u32 0) --assert=nonzero}{user32.dll i32 TranslateMessage(voidptr $msg)}{user32.dll i64 DispatchMessageW(voidptr $msg)}
Assertion failed for result: 0
> msvcrt.dll i32 wprintf(wstr "goodbye!\n")
goodbye!
> /quit
```

</details>

<details>
<summary><b>starting tf2</b></summary>

rename the executable to tf.exe

```
D:\SteamLibrary\steamapps\common\Team Fortress 2>set PATH=%PATH%;D:\SteamLibrary\steamapps\common\Team Fortress 2\bin;D:\SteamLibrary\steamapps\common\Team Fortress 2\bin\x64

D:\SteamLibrary\steamapps\common\Team Fortress 2>tf --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0)
$hInstance = 0x7FF765AB0000
> launcher.dll i32 LauncherMain(voidptr $hInstance, voidptr 0, str "-game tf", i32 1) --print-result
Setting breakpad minidump AppID = 440
SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  76561199513858240 [API loaded no]
Using breakpad crash handler
Forcing breakpad minidump interfaces to load
Looking up breakpad interfaces from steamclient
Calling BreakpadMiniDumpSystemInit
SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  76561199513858240 [API loaded yes]
SteamInternal_SetMinidumpSteamID:  Setting Steam ID:  76561199513858240
Looking up breakpad interfaces from steamclient
Calling BreakpadMiniDumpSystemInit
SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  76561199513858240 [API loaded yes]
SteamInternal_SetMinidumpSteamID:  Setting Steam ID:  76561199513858240
SteamInternal_SetMinidumpSteamID:  Caching Steam ID:  76561199513858240 [API loaded yes]
SteamInternal_SetMinidumpSteamID:  Setting Steam ID:  76561199513858240
Fontconfig error: Cannot load default config file: No such file: (null)
Unable to remove d:\steamlibrary\steamapps\common\team fortress 2\tf\textwindow_temp.html!
Result: 0
>
```

</details>

<details>
<summary><b>having fun with memory</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> /alloc 128
Allocated 128 bytes at 0x000002061136BF40
> /set 0x2061136BF40 i32 67
> /get 0x2061136BF40 i32
Value at 000002061136BF40 (i32): 67
> /free 0x2061136BF40
Freed memory at 0x000002061136BF40
> /quit

C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> /alloc 128
Allocated 128 bytes at 0x0000024F2C245990
> /set 0x0000024F2C245990 str "hello, world!"
> /get 0x0000024F2C245990 str
Value at 0x0000024F2C245990 (str): hello, world!
> /hex 0x0000024F2C245990 128
Dump of 0x0000024F2C245990 (128 bytes):
  0000024F2C245990: 68 65 6C 6C 6F 2C 20 77 6F 72 6C 64 21 00 00 00  |hello, world!...|
  0000024F2C2459A0: 6E 00 3B 00 43 00 3A 00 5C 00 55 00 73 00 65 00  |n.;.C.:.\.U.s.e.|
  0000024F2C2459B0: 72 00 73 00 5C 00 41 00 64 00 6D 00 69 00 6E 00  |r.s.\.A.d.m.i.n.|
  0000024F2C2459C0: 69 00 73 00 74 00 72 00 61 00 74 00 6F 00 72 00  |i.s.t.r.a.t.o.r.|
  0000024F2C2459D0: 5C 00 41 00 70 00 70 00 44 00 61 00 74 00 61 00  |\.A.p.p.D.a.t.a.|
  0000024F2C2459E0: 5C 00 4C 00 6F 00 63 00 61 00 6C 00 5C 00 50 00  |\.L.o.c.a.l.\.P.|
  0000024F2C2459F0: 72 00 6F 00 67 00 72 00 61 00 6D 00 73 00 5C 00  |r.o.g.r.a.m.s.\.|
  0000024F2C245A00: 4D 00 69 00 63 00 72 00 6F 00 73 00 6F 00 66 00  |M.i.c.r.o.s.o.f.|
> /free 0x0000024F2C245990
Freed memory at 0x0000024F2C245990
> /quit
```

remember to /memset your memory kids!

</details>

<details>
<summary><b>assertions</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> test.dll i32 Add(i32 0, i32 0) --print-result --assert=zero
Result: 0
> test.dll i32 Add(i32 9, i32 10) --print-result --assert=nonzero
Result: 19
> test.dll i32 Add(i32 0, i32 0) --print-result --assert=nonzero
Result: 0
Assertion failed for result: 0
> test.dll i32 Add(i32 9, i32 10) --print-result --assert=zero
Result: 19
Assertion failed for result: 19
> /quit
```

</details>

<details>
<summary><b>import by ordinal</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>dumpbin /exports C:\Windows\System32\kernel32.dll | findstr Sleep
       1481  5C8 00031980 Sleep
C:\Users\Administrator\Documents\dllfrankenstein>dumpbin /exports C:\Windows\System32\kernel32.dll | findstr Beep
        117   74 0004F780 Beep
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> kernel32.dll void #1481(i32 5000)
> kernel32.dll void #117(i32 750, i32 300)
> /quit
```

</details>

## how 2 contribute

4 spaces, not tabs  
`if (condition) {`  
test your changes  
make a pr

## how 2 contact

- maksw@maksw.pl

## faq

Q: Why make this?  
A: Because `rundll32.exe` is stupid.

Q: Is this safe?  
A: In my experience.

Q: Can I use this in production?  
A: No warranty.

Q: It crashed!  
A: Liar liar pants on fire.

Q: No i did not!  
A: Open an issue. Unless you used --normal-variables-pretty-please, then figure it out yourself.

Q: Does it support all calling conventions?  
A: Technically.

Q: Are there bugs?  
A: Features in progress.

Q: Can I do pointer math?  
A: Yes, basic math works.

Q: What is the scale of pointer math?  
A: Bytes. Always.

Q: Does it support variable arguments?  
A: Yes.

Q: Will you add fancy type parsing?  
A: How about no.
