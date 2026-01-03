# dllfrankenstein – call any DLL function anytime anywhere, a Windows ABI REPL

A raw, unapologetic bridge between man and machine.  
Manually load DLLs, manipulate memory, and invoke native functions.  
No runtime. No safety.  
scary shit

## warning

This tool is unsafe by design.  
Lying about signatures may corrupt the stack.

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
Usage in interactive/script mode is the same as non-interactive except when focused on a DLL, then you don't need to specify <dll_path>
Additional usage in interactive/script mode:
    To write a comment use ; like assembly
    /enable-normal-variables-pretty-please Enables "normal variables" in scripts or interactive mode if you forgot to add the flag
    /loaddll <path>                     Load and focus a DLL, not required mind you
    /freedll <path>                     Unload a DLL from registry
    /alloc   <size>                     Allocate memory. Provide <size> in bytes
    /free    <addr>                     Free allocated memory
    /set     <addr>     <type>  <value> Store a value at a memory address
    /memset  <addr>     <value> [count] Set a block of memory to a byte value
    /get     <addr>     <type>          Get a value from a memory address
    /hex     <addr>     [count]         Hex dump memory (default 64 bytes)
    /address <dll_path> <name>          Get a function pointer by name
    /struct  { <type> <name>, ... }     Calculate the offsets and size of a struct
    /struct  { $<name> = <type> <name>, ... } Calculate the offsets and size of a struct and assign them
    /dlls                               List loaded DLLs
    /for     <count>    {<cmd>, ...}    Repeat {} <count> times
    /repeat-until       {<cmd>, ...}    Repeat {} until assert
    /quit                               Exit the program
Variables by default are Write-Once Read-Many, no shadowing, no scopes.
    --normal-variables-pretty-please allows reassignment. Not recommended.
    $<name> = <type> <value>    Set variable value (e.g. $val = i32 10)
    $<name> = <command>         Capture command/function output into variable
    &$<name>                    Address-of: Get the memory pointer to a variable's storage
    *$<name>                    Dereference: Read 64-bit value from the address stored in $<name>
    Variables can be used as function arguments, like test.dll void print(str "%d", i32 $var1)
    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'
Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void
    Or their "proper" version: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double,
    str, wstr, voidptr are equivelant to C's "narrow" null-terminated string (char*), wide string (wchar_t*), pointer (void*, always hex)
    In the case of 'str' interpretation is entirely up to the callee (ACP, UTF-8, ASCII, or raw bytes). No validation or conversion is performed.
You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type (except pointers).
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
<summary><b>starting tf2</b></summary>

because tf2 reads from GetCommandLineA and not from lpCmdLine we have to pass parameters
 for LauncherMain in our parameters and a dummy string in LauncherMain.  
idiots.

```
D:\SteamLibrary\steamapps\common\Team Fortress 2>set PATH=%PATH%;D:\SteamLibrary\steamapps\common\Team Fortress 2\bin;D:\SteamLibrary\steamapps\common\Team Fortress 2\bin\x64

D:\SteamLibrary\steamapps\common\Team Fortress 2>caller --interactive -game tf
wilczurski's cool shit - repl
Enter command or /quit to exit.
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0)
$hInstance = 0x7FF63DAD0000
> launcher.dll i32 LauncherMain(voidptr $hInstance, voidptr 0, voidptr 0, i32 1)
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
> /quit
```

</details>

<details>
<summary><b>having fun with memory</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> $a = /alloc 128
0x159D3B19F40
$a = 0x159D3B19F40
> /set $a i32 67
Value at 0x159D3B19F40 (i32): 67
> /get $a i32
Value at 0x159D3B19F40 (i32): 67
> /set $a+4 i32 420
Value at 0x159D3B19F44 (i32): 420
> /hex $a 8
Dump of 0x159D3B19F40 (8 bytes):
  159D3B19F40: 43 00 00 00 A4 01 00 00                          |C.......|
> /free $a
Freed memory at 0x159D3B19F40
> $b = /alloc 128
0x159D3B19F20
$b = 0x159D3B19F20
> /set $b str "hello, world!"
Value at 0x159D3B19F20 (str): hello, world!
> /get $b str
Value at 0x159D3B19F20 (str): hello, world!
> /hex $b 128
Dump of 0x159D3B19F20 (128 bytes):
  159D3B19F20: 68 65 6C 6C 6F 2C 20 77 6F 72 6C 64 21 00 00 00  |hello, world!...|
  159D3B19F30: 00 00 00 00 00 00 00 00 09 00 01 08 55 99 00 10  |............U...|
  159D3B19F40: 43 00 00 00 A4 01 00 00 50 01 B0 D3 59 01 00 00  |C.......P...Y...|
  159D3B19F50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
  159D3B19F60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
  159D3B19F70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
  159D3B19F80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
  159D3B19F90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
> /free $b
Freed memory at 0x159D3B19F20
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

<details>
<summary><b>SEH covering up my ass</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> test.dll void AccessViolation()

[!!!] CRASH DETECTED DURING CALL [!!!]
Exception Code: 0xC0000005
Reason: Access Violation
> test.dll void StackOverflow()

[!!!] CRASH DETECTED DURING CALL [!!!]
Exception Code: 0xC00000FD
Reason: Stack Overflow
> test.dll void IllegalInstruction()

[!!!] CRASH DETECTED DURING CALL [!!!]
Exception Code: 0xC000001D
Reason: Illegal Instruction
> test.dll void PrivInstruction()

[!!!] CRASH DETECTED DURING CALL [!!!]
Exception Code: 0xC0000096
Reason: Privileged Instruction
> /quit
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
> ; window.ffi
> $lpfnWndProc = /address test.dll WindowProcessA
[Auto-Registered: test.dll]
0x7FFB39FE16F4
$lpfnWndProc = 0x7FFB39FE16F4
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result --assert=nonzero
Result: 0x7FF645450000
$hInstance = 0x7FF645450000
> $lpszClassName = str "mywindowclass"
$lpszClassName = 0x212EE9D7A50
> $lpWindowName = str "mywindowname"
$lpWindowName = 0x212EE9D7F90
> $hCursor = user32.dll voidptr LoadCursorA(voidptr 0, i64 32512)
$hCursor = 0x10003
> $wc_size = /struct { $style_off = u32 style, $lpfnWndProc_off = voidptr lpfnWndProc,    i32 cbClsExtra, i32 cbWndExtra, $hInstance_off = voidptr hInstance, voidptr hIcon,    $hCursor_off = voidptr hCursor, $hbrBackground_off = voidptr hbrBackground,    voidptr lpszMenuName, $lpszClassName_off = voidptr lpszClassName }
$style_off = 0x0
$lpfnWndProc_off = 0x8
$hInstance_off = 0x18
$hCursor_off = 0x28
$hbrBackground_off = 0x30
$lpszClassName_off = 0x40
Offset | Size | Align | Type    | Name
------ | ---- | ----- | ------- | --------------------
0x0000 | 4    | 4     | u32     | style
0x0004 | (4 bytes padding)
0x0008 | 8    | 8     | voidptr | lpfnWndProc
0x0010 | 4    | 4     | i32     | cbClsExtra
0x0014 | 4    | 4     | i32     | cbWndExtra
0x0018 | 8    | 8     | voidptr | hInstance
0x0020 | 8    | 8     | voidptr | hIcon
0x0028 | 8    | 8     | voidptr | hCursor
0x0030 | 8    | 8     | voidptr | hbrBackground
0x0038 | 8    | 8     | voidptr | lpszMenuName
0x0040 | 8    | 8     | voidptr | lpszClassName
Total size: 72 bytes (0x48)
Alignment: 8 bytes
$wc_size = 0x48
> $wc = /alloc $wc_size
0x212EE9D3990
$wc = 0x212EE9D3990
> /memset $wc 0 $wc_size
Set 72 bytes at 0x212EE9D3990 to 0x00
> /set $wc+$style_off u32 3
Value at 0x212EE9D3990 (u32): 3
> /set $wc+$lpfnWndProc_off voidptr $lpfnWndProc
Value at 0x212EE9D3998 (voidptr): 0x7FFB39FE16F4
> /set $wc+$hInstance_off voidptr $hInstance
Value at 0x212EE9D39A8 (voidptr): 0x7FF645450000
> /set $wc+$hCursor_off voidptr $hCursor
Value at 0x212EE9D39B8 (voidptr): 0x10003
> /set $wc+$hbrBackground_off voidptr 6
Value at 0x212EE9D39C0 (voidptr): 0x6
> /set $wc+$lpszClassName_off voidptr $lpszClassName
Value at 0x212EE9D39D0 (voidptr): 0x212EE9D7A50
> $class_atom = user32.dll u16 RegisterClassA(voidptr $wc) --print-result --assert=nonzero
Result: 49996
$class_atom = 0xC34C
> $hWnd = user32.dll voidptr CreateWindowExA(u32 0, voidptr $lpszClassName, voidptr $lpWindowName,    u32 0x00CF0000, i32 100, i32 100, i32 400, i32 200, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0x3B0B64
$hWnd = 0x3B0B64
> user32.dll i32 ShowWindow(voidptr $hWnd, i32 5)
> $msg = /alloc 48
0x212EE9FC240
$msg = 0x212EE9FC240
> /memset $msg 0 48
Set 48 bytes at 0x212EE9FC240 to 0x00
> /repeat-until {user32.dll i32 GetMessageA(voidptr $msg, voidptr 0, u32 0, u32 0) --assert=nonzero,    user32.dll i32 TranslateMessage(voidptr $msg), user32.dll i64 DispatchMessageA(voidptr $msg)}
Assertion failed for result: 0
> msvcrt.dll i32 printf(str "goodbye!\n")
goodbye!
> /free $wc
Freed memory at 0x212EE9D3990
> /free $msg
Freed memory at 0x212EE9FC240
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
0x7FFB39FE4B6F
$lpfnWndProc = 0x7FFB39FE4B6F
> $hInstance = kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result --assert=nonzero
Result: 0x7FF645450000
$hInstance = 0x7FF645450000
> $lpszClassName = wstr "mywindowclass"
$lpszClassName = 0x1F65AFC7640
> $lpWindowName = wstr "mywindowname"
$lpWindowName = 0x1F65AFC71C0
> $hCursor = user32.dll voidptr LoadCursorW(voidptr 0, i64 32512)
$hCursor = 0x10003
> $wc_size = /struct { $style_off = u32 style, $lpfnWndProc_off = voidptr lpfnWndProc,    i32 cbClsExtra, i32 cbWndExtra, $hInstance_off = voidptr hInstance, voidptr hIcon,    $hCursor_off = voidptr hCursor, $hbrBackground_off = voidptr hbrBackground,    voidptr lpszMenuName, $lpszClassName_off = voidptr lpszClassName }
$style_off = 0x0
$lpfnWndProc_off = 0x8
$hInstance_off = 0x18
$hCursor_off = 0x28
$hbrBackground_off = 0x30
$lpszClassName_off = 0x40
Offset | Size | Align | Type    | Name
------ | ---- | ----- | ------- | --------------------
0x0000 | 4    | 4     | u32     | style
0x0004 | (4 bytes padding)
0x0008 | 8    | 8     | voidptr | lpfnWndProc
0x0010 | 4    | 4     | i32     | cbClsExtra
0x0014 | 4    | 4     | i32     | cbWndExtra
0x0018 | 8    | 8     | voidptr | hInstance
0x0020 | 8    | 8     | voidptr | hIcon
0x0028 | 8    | 8     | voidptr | hCursor
0x0030 | 8    | 8     | voidptr | hbrBackground
0x0038 | 8    | 8     | voidptr | lpszMenuName
0x0040 | 8    | 8     | voidptr | lpszClassName
Total size: 72 bytes (0x48)
Alignment: 8 bytes
$wc_size = 0x48
> $wc = /alloc $wc_size
0x1F65AFC3850
$wc = 0x1F65AFC3850
> /memset $wc 0 $wc_size
Set 72 bytes at 0x1F65AFC3850 to 0x00
> /set $wc+$style_off u32 3
Value at 0x1F65AFC3850 (u32): 3
> /set $wc+$lpfnWndProc_off voidptr $lpfnWndProc
Value at 0x1F65AFC3858 (voidptr): 0x7FFB39FE4B6F
> /set $wc+$hInstance_off voidptr $hInstance
Value at 0x1F65AFC3868 (voidptr): 0x7FF645450000
> /set $wc+$hCursor_off voidptr $hCursor
Value at 0x1F65AFC3878 (voidptr): 0x10003
> /set $wc+$hbrBackground_off voidptr 6
Value at 0x1F65AFC3880 (voidptr): 0x6
> /set $wc+$lpszClassName_off voidptr $lpszClassName
Value at 0x1F65AFC3890 (voidptr): 0x1F65AFC7640
> $class_atom = user32.dll u16 RegisterClassW(voidptr $wc) --print-result --assert=nonzero
Result: 49996
$class_atom = 0xC34C
> $hWnd = user32.dll voidptr CreateWindowExW(u32 0, voidptr $lpszClassName, voidptr $lpWindowName,    u32 0x00CF0000, i32 100, i32 100, i32 400, i32 200, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0x3D0B64
$hWnd = 0x3D0B64
> user32.dll i32 ShowWindow(voidptr $hWnd, i32 5)
> $msg = /alloc 48
0x1F65AFE9A10
$msg = 0x1F65AFE9A10
> /memset $msg 0 48
Set 48 bytes at 0x1F65AFE9A10 to 0x00
> /repeat-until {user32.dll i32 GetMessageW(voidptr $msg, voidptr 0, u32 0, u32 0) --assert=nonzero,    user32.dll i32 TranslateMessage(voidptr $msg), user32.dll i64 DispatchMessageW(voidptr $msg)}
Assertion failed for result: 0
> msvcrt.dll i32 wprintf(wstr "goodbye!\n")
goodbye!
> /free $wc
Freed memory at 0x1F65AFC3850
> /free $msg
Freed memory at 0x1F65AFE9A10
> /quit
```

</details>

<details>
<summary><b>running a script; a simple opengl 1.1 triangle</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --script gl.ffi
wilczurski's cool shit - script
> ; gl.ffi
> /loaddll opengl32.dll
Loaded and registered: opengl32.dll (Focus set)
> glfw3.dll void glfwInit()
> $w = glfw3.dll voidptr glfwCreateWindow(i32 640, i32 480, str "Triangle", voidptr 0, voidtpr 0) --print-result --assert=nonzero
Result: 0x14F2DD2CAC0
$w = 0x14F2DD2CAC0
> glfw3.dll void glfwMakeContextCurrent(voidptr $w)
> /repeat-until { glfw3.dll i32 glfwWindowShouldClose(voidptr $w) --assert=zero,    void glClear(u32 0x4000),    void glBegin(u32 0x4),     void glVertex2f(f32 -0.5f, f32 -0.5f),     void glVertex2f(f32 0.5f, f32 -0.5f),     void glVertex2f(f32 0.0f, f32 0.5f),    void glEnd(),    void glFlush(),    glfw3.dll void glfwSwapBuffers(voidptr $w),    glfw3.dll void glfwPollEvents() }
Assertion failed for result: 1
> msvcrt.dll i32 printf(str "goodbye!\n")
goodbye!
> /quit
```

</details>

<details>
<summary><b>running a script; a complex opengl 3.3 triangle</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --script gl2.ffi
wilczurski's cool shit - script
> ; gl2.ffi
> ;#define GL_COLOR_BUFFER_BIT 0x4000
> ;#define GL_TRIANGLES        0x0004
> ;#define GL_ARRAY_BUFFER     0x8892
> ;#define GL_STATIC_DRAW      0x88E4
> ;#define GL_VERTEX_SHADER    0x8B31
> ;#define GL_FRAGMENT_SHADER  0x8B30
> ;#define GL_FLOAT            0x1406
> ;#define GL_FALSE            0
> $GL_COLOR_BUFFER_BIT = i32 0x4000
$GL_COLOR_BUFFER_BIT = 0x4000
> $GL_TRIANGLES        = i32 0x0004
$GL_TRIANGLES = 0x4
> $GL_ARRAY_BUFFER     = i32 0x8892
$GL_ARRAY_BUFFER = 0x8892
> $GL_STATIC_DRAW      = i32 0x88E4
$GL_STATIC_DRAW = 0x88E4
> $GL_VERTEX_SHADER    = i32 0x8B31
$GL_VERTEX_SHADER = 0x8B31
> $GL_FRAGMENT_SHADER  = i32 0x8B30
$GL_FRAGMENT_SHADER = 0x8B30
> $GL_FLOAT            = i32 0x1406
$GL_FLOAT = 0x1406
> $GL_FALSE            = i32 0
$GL_FALSE = 0x0
> ;const char* vertex_shader_src =
> ;    "#version 330 core\n"
> ;    "layout (location = 0) in vec2 aPos;\n"
> ;    "void main() {\n"
> ;    "   gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);\n"
> ;    "}";
> ;
> ;const char* fragment_shader_src =
> ;    "#version 330 core\n"
> ;    "out vec4 FragColor;\n"
> ;    "void main() {\n"
> ;    "   FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n" // Orange color
> ;    "}";
> $vertex_shader_src = str "      #version 330 core\n      layout (location = 0) in vec2 aPos;\n      void main() {\n          gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);\n      }"
$vertex_shader_src = 0x298FC376C90
> $fragment_shader_src = str "      #version 330 core\n      out vec4 FragColor;\n      void main() {\n         FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n      }"
$fragment_shader_src = 0x298FC376D40
> ;int32_t main(void) {
> ;    glfwInit();
> ;    w = glfwCreateWindow(640, 480, "Modern Triangle", 0, 0);
> ;    glfwMakeContextCurrent(w);
> /loaddll glfw3.dll
Loaded and registered: glfw3.dll (Focus set)
> i32 glfwInit()
> $w = voidptr glfwCreateWindow(i32 640, i32 480, str "Modern Triangle", voidptr 0, voidptr 0) --assert=nonzero
$w = 0x298FC3AA240
> void glfwMakeContextCurrent(voidptr $w)
> ;void load_gl_functions() {
> ;    glClear = (PFNGLCLEARPROC)glfwGetProcAddress("glClear");
> ;    glDrawArrays = (PFNGLDRAWARRAYSPROC)glfwGetProcAddress("glDrawArrays");
> ;    glGenBuffers = (PFNGLGENBUFFERSPROC)glfwGetProcAddress("glGenBuffers");
> ;    glBindBuffer = (PFNGLBINDBUFFERPROC)glfwGetProcAddress("glBindBuffer");
> ;    glBufferData = (PFNGLBUFFERDATAPROC)glfwGetProcAddress("glBufferData");
> ;    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)glfwGetProcAddress("glGenVertexArrays");
> ;    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)glfwGetProcAddress("glBindVertexArray");
> ;    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)glfwGetProcAddress("glEnableVertexAttribArray");
> ;    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)glfwGetProcAddress("glVertexAttribPointer");
> ;    glCreateShader = (PFNGLCREATESHADERPROC)glfwGetProcAddress("glCreateShader");
> ;    glShaderSource = (PFNGLSHADERSOURCEPROC)glfwGetProcAddress("glShaderSource");
> ;    glCompileShader = (PFNGLCOMPILESHADERPROC)glfwGetProcAddress("glCompileShader");
> ;    glCreateProgram = (PFNGLCREATEPROGRAMPROC)glfwGetProcAddress("glCreateProgram");
> ;    glAttachShader = (PFNGLATTACHSHADERPROC)glfwGetProcAddress("glAttachShader");
> ;    glLinkProgram = (PFNGLLINKPROGRAMPROC)glfwGetProcAddress("glLinkProgram");
> ;    glUseProgram = (PFNGLUSEPROGRAMPROC)glfwGetProcAddress("glUseProgram");
> ;}
> $glClear = voidptr glfwGetProcAddress(str "glClear") --assert=nonzero
$glClear = 0x7FFAE9164E80
> $glDrawArrays = voidptr glfwGetProcAddress(str "glDrawArrays") --assert=nonzero
$glDrawArrays = 0x7FFAFE1BA2E0
> $glGenBuffers = voidptr glfwGetProcAddress(str "glGenBuffers") --assert=nonzero
$glGenBuffers = 0x7FFAFE1BFBA0
> $glBindBuffer = voidptr glfwGetProcAddress(str "glBindBuffer") --assert=nonzero
$glBindBuffer = 0x7FFAFE1AAF60
> $glBufferData = voidptr glfwGetProcAddress(str "glBufferData") --assert=nonzero
$glBufferData = 0x7FFAFE1AD5E0
> $glGenVertexArrays = voidptr glfwGetProcAddress(str "glGenVertexArrays") --assert=nonzero
$glGenVertexArrays = 0x7FFAFE1C0920
> $glBindVertexArray = voidptr glfwGetProcAddress(str "glBindVertexArray") --assert=nonzero
$glBindVertexArray = 0x7FFAFE1AC1E0
> $glEnableVertexAttribArray = voidptr glfwGetProcAddress(str "glEnableVertexAttribArray") --assert=nonzero
$glEnableVertexAttribArray = 0x7FFAFE1BC6E0
> $glVertexAttribPointer = voidptr glfwGetProcAddress(str "glVertexAttribPointer") --assert=nonzero
$glVertexAttribPointer = 0x7FFAFE203E60
> $glCreateShader = voidptr glfwGetProcAddress(str "glCreateShader") --assert=nonzero
$glCreateShader = 0x7FFAFE1B6FE0
> $glShaderSource = voidptr glfwGetProcAddress(str "glShaderSource") --assert=nonzero
$glShaderSource = 0x7FFAFE1EA1E0
> $glCompileShader = voidptr glfwGetProcAddress(str "glCompileShader") --assert=nonzero
$glCompileShader = 0x7FFAFE1B4260
> $glDeleteShader = voidptr glfwGetProcAddress(str "glDeleteShader") --assert=nonzero
$glDeleteShader = 0x7FFAFE1B89E0
> $glCreateProgram = voidptr glfwGetProcAddress(str "glCreateProgram") --assert=nonzero
$glCreateProgram = 0x7FFAFE1B6BE0
> $glAttachShader = voidptr glfwGetProcAddress(str "glAttachShader") --assert=nonzero
$glAttachShader = 0x7FFAFE1AA4A0
> $glLinkProgram = voidptr glfwGetProcAddress(str "glLinkProgram") --assert=nonzero
$glLinkProgram = 0x7FFAFE1D0F20
> $glUseProgram = voidptr glfwGetProcAddress(str "glUseProgram") --assert=nonzero
$glUseProgram = 0x7FFAFE1F8AE0
> $glGetShaderiv = voidptr glfwGetProcAddress(str "glGetShaderiv") --assert=nonzero
$glGetShaderiv = 0x7FFAFE1C8EA0
> $glGetShaderInfoLog = voidptr glfwGetProcAddress(str "glGetShaderInfoLog") --assert=nonzero
$glGetShaderInfoLog = 0x7FFAFE1C8CA0
> $glGetProgramiv = voidptr glfwGetProcAddress(str "glGetProgramiv") --assert=nonzero
$glGetProgramiv = 0x7FFAFE1C7B20
> $glGetProgramInfoLog = voidptr glfwGetProcAddress(str "glGetProgramInfoLog") --assert=nonzero
$glGetProgramInfoLog = 0x7FFAFE1C6F20
> ; main
> ;    float vertices[] = {
> ;        -0.5f, -0.5f,
> ;         0.5f, -0.5f,
> ;         0.0f,  0.5f
> ;    };
> ; for loops with $i coming soonTM
> $vertices = /alloc 24
0x298FF06B3C0
$vertices = 0x298FF06B3C0
> /set $vertices f32 -0.5
Value at 0x298FF06B3C0 (f32): -0.500000
> /set $vertices + 4 f32 -0.5
Value at 0x298FF06B3C4 (f32): -0.500000
> /set $vertices + 8 f32 0.5
Value at 0x298FF06B3C8 (f32): 0.500000
> /set $vertices + 12 f32 -0.5
Value at 0x298FF06B3CC (f32): -0.500000
> /set $vertices + 16 f32 0.0
Value at 0x298FF06B3D0 (f32): 0.000000
> /set $vertices + 20 f32 0.5
Value at 0x298FF06B3D4 (f32): 0.500000
> ;    unsigned int VBO, VAO;
> ;    glGenVertexArrays(1, &VAO);
> ;    glGenBuffers(1, &VBO);
> $VAO = u32 0
$VAO = 0x0
> $VBO = u32 0
$VBO = 0x0
> void $glGenVertexArrays(i32 1, voidptr &$VAO)
> void $glGenBuffers(i32 1, voidptr &$VBO)
> ;    glBindVertexArray(VAO);
> ;    glBindBuffer(GL_ARRAY_BUFFER, VBO);
> ;    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
> void $glBindVertexArray(u32 $VAO)
> void $glBindBuffer(u32 $GL_ARRAY_BUFFER, u32 $VBO)
> void $glBufferData(u32 $GL_ARRAY_BUFFER, i64 24, voidptr $vertices, u32 $GL_STATIC_DRAW)
> ;    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
> ;    glEnableVertexAttribArray(0);
> void $glVertexAttribPointer(u32 0, i32 2, u32 $GL_FLOAT, u8 0, i32 8, voidptr 0)
> void $glEnableVertexAttribArray(u32 0)
> ;    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
> ;    glShaderSource(vertexShader, 1, &vertex_shader_src, 0);
> ;    glCompileShader(vertexShader);
> $vertexShader = u32 $glCreateShader(u32 $GL_VERTEX_SHADER) --assert=nonzero
$vertexShader = 0x1
> void $glShaderSource(u32 $vertexShader, i32 1, voidptr &$vertex_shader_src, voidptr 0)
> void $glCompileShader(u32 $vertexShader)
> ;    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
> ;    glShaderSource(fragmentShader, 1, &fragment_shader_src, 0);
> ;    glCompileShader(fragmentShader);
> $fragmentShader = u32 $glCreateShader(u32 $GL_FRAGMENT_SHADER) --assert=nonzero
$fragmentShader = 0x2
> void $glShaderSource(u32 $fragmentShader, i32 1, voidptr &$fragment_shader_src, voidptr 0)
> void $glCompileShader(u32 $fragmentShader)
> ;    unsigned int shaderProgram = glCreateProgram();
> ;    glAttachShader(shaderProgram, vertexShader);
> ;    glAttachShader(shaderProgram, fragmentShader);
> ;    glLinkProgram(shaderProgram);
> $prog = u32 $glCreateProgram()
$prog = 0x3
> void $glAttachShader(u32 $prog, u32 $vertexShader)
> void $glAttachShader(u32 $prog, u32 $fragmentShader)
> void $glLinkProgram(u32 $prog)
> ;    glDeleteShader(vertexShader);
> ;    glDeleteShader(fragmentShader);
> void $glDeleteShader(u32 $vertexShader)
> void $glDeleteShader(u32 $fragmentShader)
> ;    while (!glfwWindowShouldClose(w)) {
> ;        glClear(GL_COLOR_BUFFER_BIT);
> ;        glUseProgram(shaderProgram);
> ;        glBindVertexArray(VAO);
> ;        glDrawArrays(GL_TRIANGLES, 0, 3);
> ;        glfwSwapBuffers(w);
> ;        glfwPollEvents();
> ;    }
> /repeat-until { i32 glfwWindowShouldClose(voidptr $w) --assert=zero,      void $glClear(u32 $GL_COLOR_BUFFER_BIT),      void $glUseProgram(u32 $prog),      void $glBindVertexArray(u32 $VAO),      void $glDrawArrays(u32 $GL_TRIANGLES, i32 0, i32 3),      void glfwSwapBuffers(voidptr $w),      void glfwPollEvents() }
Assertion failed for result: 1
> ; cleanup
> /free $vertices
Freed memory at 0x298FF06B3C0
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
A: Open an issue. **Describe it well.**

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
