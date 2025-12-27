# ffi by me

i'm terrible at writing readmes, just make a pr

```
C:\Users\maksw\Documents\dllfrankenstein>caller
wilczurski's cool shit - ffi
Usage: caller.exe <dll_path> <ret> <func>(<args>) [--print-result]
   or: caller.exe --interactive
   Available commands in interactive mode:
     /loaddll <path>       Load and focus a DLL
     /freedll <path>      Unload a DLL from registry
     /quit                Exit the program
    Usage in interactive mode is the same as non-interactive.
    Types: i8, i16, i32, i64, u8, u16, u32, u64
    equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t
    f32, f64, str, voidptr, void
    equivalent to float, double, null-terminated string, pointer (hex), and void
```
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
