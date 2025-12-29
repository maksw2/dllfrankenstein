# dllfrankenstein – call any DLL function anytime anywhere

dllfrankenstein lets you call arbitrary DLL functions with explicit type control, direct memory access, and an optional interactive REPL.  
scary shit

## warning

This tool is unsafe by design.
Lying about signatures will corrupt the stack or crash the process.

## how 2 use it

to build the app:
- `cl /c /Zi caller.c`
- `ml64 /c helper.asm`
- `link caller.obj helper.obj /debug /out:caller.exe`

to build the test dll:
- `cl /LD /Zi test.c /link /debug`

### to run it:

normal mode:

- `./caller.exe <dll> <return-type> <func-name>(<arg-type> <arg-value>, ...) [--print-return]`

interactive mode:

- `./caller.exe --interactive`
- `/loaddll <dll>`
- `<optional dll name if not in focus> <return-type> <func-name>(<arg-type> <arg-value>, ...)`

## help string

```
C:\Users\Administrator\Documents\dllfrankenstein>caller
wilczurski's cool shit - repl + ffi
Usage: caller.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...) [--print-result] [--assert=<type>]
    or: caller.exe --interactive
    <type> can be: zero, nonzero, negative, nonnegative
    not specifying means none
Usage in interactive mode:
    /loaddll <path>                     Load and focus a DLL
    /freedll <path>                     Unload a DLL from registry
    /alloc   <size>                     Allocate memory. Provide <size> in bytes
    /free    <addr>                     Free allocated memory
    /set     <addr>     <type>  <value> Store a value at a memory address
    /memset  <addr>     <value> [count] Set a block of memory to a byte value
    /get     <addr>     <type>          Get a value from a memory address
    /hex     <addr>     [count]         Hex dump memory (default 64 bytes)
    /address <dll_path> <name>          Get a function pointer by name
    /quit                               Exit the program
Variables are Write-Once Read-Many, no shadowing, no scopes.
    $<name> = <type> <value>            Assign a variable
    Variables can be used as function arguments, like test.dll void print(str "%d", $var1)
    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'
Usage in interactive mode is the same as non-interactive except for the focused DLL,
then you don't need to specify <dll_path>
Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, voidptr, void
    Equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t,
    float, double, null-terminated string, pointer (always hex), and void
You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type.
```

## example; creating a win32 window

```
C:\Users\Administrator\Documents\dllfrankenstein>caller --interactive
--- Interactive DLL Caller ---
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
> $hwnd = user32.dll voidptr CreateWindowExA(u32 0, voidptr $lpszClassName, str "mywindowname", u32 0x00CF0000, i32 100, i32 100, i32 100, i32 100, voidptr 0, voidptr 0, voidptr $hInstance, voidptr 0) --print-result --assert=nonzero
Result: 0x7d0f02
$hwnd = 0x7d0f02
> user32.dll i32 ShowWindow(voidptr $hwnd, i32 5)
```

## example; having fun with memory

```
C:\Users\maksw\Documents\dllfrankenstein>caller --interactive
--- Interactive DLL Caller ---
Enter command or /quit to exit.
> /alloc 128
Allocated 128 bytes at 000002061136BF40
> /set 0x2061136BF40 i32 67
> /get 0x2061136BF40 i32
Value at 000002061136BF40 (i32): 67
> /free 0x2061136BF40
Freed memory at 000002061136BF40
> /quit

C:\Users\maksw\Documents\dllfrankenstein>caller --interactive
--- Interactive DLL Caller ---
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

## example; assertions

```
C:\Users\maksw\Documents\dllfrankenstein>caller --interactive
--- Interactive DLL Caller ---
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

## how 2 contribute

4 spaces, not tabs  
`if (condition) {`  
test your changes  
make a pr

## how 2 contact

- maksw@maksw.pl

Q: Why make this?  
A: Because `rundll32.exe` is too limiting.

Q: Is this safe?  
A: As long as the signature is valid.

Q: Can I use this in production?  
A: No warranty.

Q: Does it support all calling conventions?  
A: Technically.

Q: Can I do pointer math? A: Yes, basic arithmetic like $base + 0x40 works.

Q: Does it support variable arguments?  
A: Yes.

Q: Will you add fancy type parsing?  
A: How about no.
