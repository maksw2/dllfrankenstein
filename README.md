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
C:\Users\maksw\Documents\dllfrankenstein>caller
wilczurski's cool shit - ffi
Usage: caller.exe <dll_path> <ret> <func>(<args>) [--print-result] [--assert=<type>]
    <type> can be: zero, nonzero, negative, nonnegative
    not specifying defaults to none
    or: caller.exe --interactive
    Available commands in interactive mode:
      /loaddll <path>                     Load and focus a DLL
      /freedll <path>                     Unload a DLL from registry
      /alloc   <size in bytes>            Allocate memory
      /free    <addr>                     Free allocated memory
      /set     <addr>     <type>  <value> Store a value at a memory address
      /get     <addr>     <type>          Get a value from a memory address
      /memset  <addr>     <value> <count> Set a block of memory to a byte value
      /address <dll_path> <name>          Get a function pointer by name
      /quit                               Exit the program
    Usage in interactive mode is the same as non-interactive with the exception of the focused dll, then no need to specify <dll_path>
    Types: i8, i16, i32, i64, u8, u16, u32, u64
    Equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t
    f32, f64, str, voidptr, void
    Equivalent to float, double, null-terminated string, pointer (hex), and void
```

## example; creating a win32 window

```
C:\Users\maksw\Documents\dllfrankenstein>caller.exe --interactive
--- Interactive DLL Caller ---
Enter command or /quit to exit.
> /loaddll test.dll
Loaded and registered: test.dll (Focus set)
> test.dll voidptr gimmewindowclass(str "mywindowclass") --print-result
Result: 0x12c1f23f9c0
> /loaddll user32.dll
Loaded and registered: user32.dll (Focus set)
> /loaddll kernel32.dll
Loaded and registered: kernel32.dll (Focus set)
> kernel32.dll voidptr GetModuleHandleA(i64 0) --print-result
Result: 0x7ff68f4b0000
> user32.dll u16 RegisterClassA(voidptr 0x12c1f23f9c0) --print-result
Result: 49895
> user32.dll voidptr CreateWindowExA(u32 0, str "mywindowclass", str "mywindowname", u32 0x00CF0000, i32 100, i32 100, i32 100, i32 100, voidptr 0, voidptr 0, voidptr 0x7ff68f4b0000, voidptr 0) --print-result
Result: 0x2037e
> user32.dll i32 ShowWindow(voidptr 0x2037e, i32 5)
> test.dll void PumpEvents()
> user32.dll void DestroyWindow(voidptr 0x2037e)
> test.dll void print(str "it all works!\n")
it all works!
> test.dll void print(str "%s %s %s\n", str "varargs", str "work", str "too")
varargs work too
> /quit
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

- make a pr
- bonus points if it doesn’t crash

## how 2 contact

- maksw@maksw.pl

Q: Why make this?  
A: Because `rundll32.exe` is too limiting.  

Q: Is this safe?  
A: As long as the signature is valid. 

Q: Can I use this in production?  
A: No warranty.

Q: Does it support all calling conventions?  
A: It's x86_64 dumbass.

Q: Does it support variable arguments?  
A: Yes. Look at the example.

Q: Will you add fancy type parsing?  
A: How about no.
