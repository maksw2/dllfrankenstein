# dllfrankenstein

Call any DLL function anytime anywhere  
rundll32's evil, competent cousin  
With great power comes great segfaults.  

## how 2 use it

to build the app:
- `build.bat`

wow, a windows tool uses a windows scripting language for build 👻

## help string

```
wilczurski's cool shit - ffi

USAGE
  invoke.exe --interactive | -i [--quiet]
  invoke.exe --script <file.ffi> [--quiet]
  invoke.exe --check <file.ffi>
  invoke.exe --eval <source> [--quiet]
  invoke.exe <source...>

OPTIONS
  --quiet - suppress normal result, dump, and list output
  --check <file.ffi> - parse and type-check a script without executing it
  --interactive, -i - start the REPL
  --help, -h, -?, /h, /? - show this reference

SOURCE
  ; comment - comment to end of line
  // comment - comment to end of line
  Semicolons start comments; they are not statement terminators.
  Statements do not require terminators.

TYPES
  i8, i16, i32, i64 - signed integers
  u8, u16, u32, u64 - unsigned integers
  f32, f64 - floating point
  str, wstr - narrow and wide null-terminated string pointers
  ptr - untyped pointer
  void - return type only

VALUES AND VARIABLES
  $x = i32 42
  $x = i32 ($y + 1)
  $p = ptr &$x
  $next = $p + 8
  $bytes = $end - $begin
  $v = i32 @$p
  i32 @$p = $v

  Blocks create variable scopes. Assigning an existing name redefines that variable in
  the scope where it already exists. New names are created in the current scope.
  Scalar variables adopt the value and type of the right-hand side when assigned.
  i32 $x numerically converts $x; i32 @&$x reads the same storage as i32 instead.
  Pointer arithmetic is always in bytes.

FUNCTIONS
  i32 add(i32 $a, i32 $b) {
      return $a + $b
  }

  add(1, 2) - call a previously defined script function
  i32 add(i32 1, i32 2) - call with a caller-provided signature
  i32 SomeExport(i32 1) - call the focused DLL and remember its signature/origin
  user32.dll i32 SomeExport(i32 1) - call a specific DLL and remember it as the origin
  SomeExport(2) - reuse the remembered FFI signature and origin
  i32 $fn(i32 1) - call an address stored in a variable and remember its signature/origin
  $fn(2) - reuse the remembered signature while reading the current address from $fn
  &add - get the function's address

  A fully typed FFI call declares/redeclares a global remembered call descriptor.
  Later untyped calls convert arguments to that signature and reuse its DLL/variable origin.
  A same-name script function takes priority over a remembered FFI function for bare named calls.
  An unknown bare call reports: no known function 'Name'.
  Redefining a function does not change its memory position.

STRUCTS, UNIONS, ARRAYS
  struct POINT {
      i32 x
      i32 y
  }

  struct PACKED pack(1) {
      u8 tag
      u32 value
  }

  union NUMBER {
      i64 integer
      f64 floating
  }

  struct BUFFER {
      u32 length
      u8 data[256]
  }

  $p = POINT { .x = 10, .y = 20 }
  $x = $p.x
  $p.x = 30
  $b = $buffer.data[4]
  $tmp = struct { i32 x i32 y } { .x = 1, .y = 2 }

  sizeof(POINT) - size of a type in bytes
  sizeof($p) - size of a value in bytes
  alignof(POINT) - alignment of a type
  offsetof(POINT, x) - byte offset of a field; nested paths are allowed

  Named aggregate types must be defined before use.
  Redefining a named aggregate creates a new type definition.
  Aggregate initializer fields are comma-separated; unspecified fields are zeroed.

ARRAYS
  $vertices = f32[6] { -0.5, -0.5, 0.5, -0.5, 0.0, 0.5 }
  $flakes = SnowFlake[$MAX_FLAKES] - runtime-sized, uninitialized storage
  $scratch = u8[4096] {} - an initializer zeroes omitted elements
  $vertices[2] = 0.25
  $x = $vertices[2]
  sizeof($vertices) - total byte size of the array
  &$vertices - address of the array storage

  Array extents may be integer expressions and are evaluated when the array is allocated.
  No initializer leaves array storage uninitialized; {} explicitly zero-initializes it.
  Array initializer elements are comma-separated, converted to the element type, and the remainder is zeroed.
  [N] is part of the type, so sizeof(f32[6]), sizeof(i32[$n]), and f32[6] @$address are valid.
  Runtime-sized arrays cannot be aggregate fields because aggregate layout must remain fixed.

CONTROL FLOW
  if ($x) { ... }
  if ($x) { ... } else if ($y) { ... } else { ... }
  switch ($x) {
    case $a { ... }
    case 2, $b + 1 { ... }
    default { ... }
  }
  while ($x) { ... }
  break
  continue
  return
  return $value
  assert($condition)
  assert($condition, "message")

  if, switch arms, and while bodies require braces. Zero is false; nonzero is true.
  switch evaluates its subject once, then case expressions left-to-right and top-to-bottom.
  Case expressions are runtime scalar expressions, converted to the subject's current type before comparison.
  The first matching arm runs; there is no fallthrough or required break. default is optional and must be last.
  break and continue are valid only inside while.
  return is valid only inside a function.

OPERATORS
  unary - - ! ~ &
  arithmetic - + - * / %
  shifts - << >>
  comparison - == != < <= > >=
  bitwise - & ^ |
  logical - && ||

MEMORY
  $value = i32 @$address - read an i32 from an address
  i32 @$address = 123 - write an i32 to an address
  f32 @($address + 4) = 1.0 - typed memory access may use any scalar address expression
  hex($address, 64) - dump 64 bytes from an address

  The memory access type controls the width and interpretation of the access.
  The address expression is not converted: its raw scalar bits are used as the address.
  Assignment to a typed memory place converts the right-hand side to the place type.
  Pointer arithmetic is always in bytes.

DLLS
  load("foo.dll") - load a DLL and make it focused
  focus("foo.dll") - focus a DLL, loading it if necessary
  free("foo.dll") - unload a DLL
  dlls() - list loaded DLLs; * marks the focused DLL
  address("foo.dll", "Symbol") - get an exported symbol address
  address("foo.dll", "#123") - get an export by ordinal
  quit() or quit($code) - exit the interpreter or REPL with an optional i32 exit code
```

## examples

<details>
<summary><b>a simple messagebox</b></summary>

```
invoke.exe user32.dll i32 MessageBoxA(ptr 0, str "hi", str "title", u32 0)
invoke.exe user32.dll i32 MessageBoxW(ptr 0, wstr "hi", wstr "title", u32 0)
```

</details>

<details>
<summary><b>starting tf2</b></summary>

because tf2 reads from GetCommandLineA and not from lpCmdLine we have to pass parameters
 for LauncherMain in our parameters and a dummy string in LauncherMain.  
idiots.

![alt text](readme-images/image-6.png)

</details>

<details>
<summary><b>import by ordinal</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>dumpbin /exports C:\Windows\System32\kernel32.dll | findstr Sleep
       1481  5C8 00031980 Sleep
C:\Users\Administrator\Documents\dllfrankenstein>dumpbin /exports C:\Windows\System32\kernel32.dll | findstr Beep
        117   74 0004F780 Beep
C:\Users\Administrator\Documents\dllfrankenstein>invoke.exe --interactive
wilczurski's cool shit - repl
Enter command or /quit to exit.
> kernel32.dll void #1481(u32 5000)
> kernel32.dll i32 #117(u32 750, u32 300)
> quit()
```

</details>

<details>
<summary><b>SEH covering up my ass</b></summary>

```
C:\Users\Administrator\Documents\dllfrankenstein>invoke.exe --interactive
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
> quit()
```

</details>

<details>
<summary><b>creating a win32 window</b></summary>

![alt text](readme-images/image-5.png)

</details>

### scripts

<details>
<summary><b>creating a win32 window</b></summary>

![alt text](readme-images/image.png)

</details>

<details>
<summary><b>creating a win32 window using wide strings</b></summary>

![alt text](readme-images/image-1.png)

</details>

<details>
<summary><b>win32 snow</b></summary>

![alt text](readme-images/image-8.png)

</details>

<details>
<summary><b>a simple opengl 1.1 triangle</b></summary>

![alt text](readme-images/image-2.png)

</details>

The following examples are intentionally hostile to the tool's design.
They exist to show limits, not best practices.

<details>
<summary><b>a complex opengl 3.3 triangle</b></summary>

![alt text](readme-images/image-3.png)

</details>

<details>
<summary><b>a complex opengl 3.3 triangle without glfw to help</b></summary>

![alt text](readme-images/image-4.png)

</details>

<details>
<summary><b>a complex opengl 3.3 triangle with controls</b></summary>

![alt text](readme-images/image-7.gif)

</details>

<details>
<summary><b>an http server</b></summary>

![alt text](readme-images/image.webp)

</details>

## how 2 contribute

4 spaces, not tabs  
k&r braces  
pointer belongs to the type  
test your changes  
make a pr

## how 2 contact

- maksw@maksw.pl

## faq

Q: Why make this?  
A: Because `rundll32.exe` is stupid. Everything rundll32 can do, this tool does better and more

Q: What's the use case for it?  
A: Poking random DLLs without writing C.

Q: Is this safe?  
A: In my experience.  

Q: Can this brick my system?  
A: If you're creative enough.

Q: Can I use this in production?  
A: No warranty.

Q: Why is the syntax like this?  
A: It made sense at 3 AM.

Q: What inspired the syntax?  
A: Alcohol.

Q: It crashed!  
A: Probably your fault.

Q: Does it support all calling conventions?  
A: Technically.

Q: Are there bugs?  
A: Features in progress.

Q: Can I do pointer math?  
A: Haven't you read the help string?

Q: Does it support variable arguments?  
A: Variadic argument promotion does not exist. You need to know which type to cast to.

Q: Will you add fancy type parsing?  
A: Let's circle back on this in Q4.
