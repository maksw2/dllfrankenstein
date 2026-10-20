#include "checker.hpp"
#include "interpreter.hpp"
#include "parser.hpp"
#include "platform.hpp"
#include "runtime.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {
std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed to open '" + path + "'");
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

void print_diagnostic(const std::string& source_name, const std::string& source, const DiagnosticError& error, const char* prefix = "error") {
    std::fprintf(stderr, "%s:%u:%u: %s: %s\n", source_name.c_str(), error.span.line, error.span.column, prefix, error.what());
    if (error.span.line == 0) return;
    size_t begin = 0;
    uint32_t line = 1;
    while (line < error.span.line && begin < source.size()) {
        const size_t next = source.find('\n', begin);
        if (next == std::string::npos) return;
        begin = next + 1;
        ++line;
    }
    size_t end = source.find('\n', begin);
    if (end == std::string::npos) end = source.size();
    const std::string_view text(source.data() + begin, end - begin);
    std::fprintf(stderr, "  %.*s\n  ", static_cast<int>(text.size()), text.data());
    for (uint32_t i = 1; i < error.span.column; ++i) std::fputc(' ', stderr);
    std::fputc('^', stderr);
    const uint32_t width = std::max<uint32_t>(1, error.span.length);
    for (uint32_t i = 1; i < width && error.span.column + i <= text.size(); ++i) std::fputc('~', stderr);
    std::fputc('\n', stderr);
}

void print_warnings(const std::string& source_name, const std::vector<Warning>& warnings) {
    for (const auto& warning : warnings)
        std::fprintf(stderr, "%s:%u:%u: warning: %s\n", source_name.c_str(), warning.span.line, warning.span.column, warning.message.c_str());
}

void check_only(const std::string& source, const std::string& name) {
    auto module = parse_module(source, name);
    TypeRegistry types;
    CheckResult result = check_module(*module, types, {});
    print_warnings(name, result.warnings);
}

void check_and_execute(Runtime& runtime, const std::string& source, const std::string& name, bool echo_calls) {
    auto module = parse_module(source, name);
    CheckResult result = check_module(*module, runtime.types(), runtime.semantic_snapshot());
    print_warnings(name, result.warnings);
    // Typed FFI calls declare module-global remembered call descriptors during
    // checking, even when they occur inside a function or an untaken branch.
    runtime.install_ffi_bindings(result.resulting_state.ffi_functions);
    Interpreter(runtime).execute_module(module, echo_calls);
}

void repl(bool quiet) {
    Runtime runtime(quiet);
    std::puts("dllfrankenstein repl");
    std::puts("Use quit() or quit(code) to exit.");
    std::string buffer;
    bool incomplete = false;
    uint64_t chunk = 1;
    while (true) {
        std::fputs(incomplete ? "... " : "> ", stdout);
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (!buffer.empty()) buffer.push_back('\n');
        buffer += line;
        const std::string name = "<repl:" + std::to_string(chunk) + ">";
        try {
            check_and_execute(runtime, buffer, name, true);
            buffer.clear();
            incomplete = false;
            ++chunk;
        } catch (const IncompleteInput&) {
            incomplete = true;
        } catch (const DiagnosticError& e) {
            print_diagnostic(name, buffer, e);
            buffer.clear();
            incomplete = false;
            ++chunk;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: runtime error: %s\n", name.c_str(), e.what());
            buffer.clear();
            incomplete = false;
            ++chunk;
        }
    }
}

void help() {
    std::puts(R"(wilczurski's cool shit - ffi

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
      i32 x,
      i32 y,
  }

  struct PACKED pack(1) {
      u8 tag,
      u32 value,
  }

  union NUMBER {
      i64 integer,
      f64 floating,
  }

  struct BUFFER {
      u32 length,
      u8 data[256],
  }

  $p = POINT { .x = 10, .y = 20 }
  $x = $p.x
  $p.x = 30
  $b = $buffer.data[4]
  $tmp = struct { i32 x, i32 y } { .x = 1, .y = 2 }

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
  quit() or quit($code) - exit the interpreter or REPL with an optional i32 exit code)");
}
}

int main(int argc, char** argv) {
    bool quiet = false;
    bool interactive = false;
    std::string script_path;
    std::string check_path;
    std::string eval_source;
    std::vector<std::string> one_shot;
    int first_one_shot_arg = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (first_one_shot_arg >= 0) {
            one_shot.push_back(arg);
            continue;
        }
        if (arg == "--help" || arg == "-h" || arg == "-?" || arg == "/h" || arg == "/?") { help(); return 0; }
        if (arg == "--quiet") { quiet = true; continue; }
        if (arg == "--interactive" || arg == "-i") { interactive = true; continue; }
        if (arg == "--script") {
            if (++i >= argc) { std::fputs("--script requires a path\n", stderr); return 2; }
            script_path = argv[i]; continue;
        }
        if (arg == "--check") {
            if (++i >= argc) { std::fputs("--check requires a path\n", stderr); return 2; }
            check_path = argv[i]; continue;
        }
        if (arg == "--eval") {
            if (++i >= argc) { std::fputs("--eval requires source text\n", stderr); return 2; }
            eval_source = argv[i]; continue;
        }
        first_one_shot_arg = i;
        one_shot.push_back(arg);
    }

    const int modes = static_cast<int>(interactive) + !script_path.empty() + !check_path.empty() + !eval_source.empty() + !one_shot.empty();
    if (modes == 0) { std::fputs("no input; use --help for usage\n", stderr); return 2; }
    if (modes != 1) { std::fputs("choose exactly one of --interactive, --script, --check, --eval, or one-shot source\n", stderr); return 2; }

    try {
        if (interactive) { repl(quiet); return 0; }
        if (!check_path.empty()) {
            const std::string source = read_file(check_path);
            try { check_only(source, check_path); }
            catch (const IncompleteInput&) { throw DiagnosticError("unexpected end of input", {static_cast<uint32_t>(source.size()), 0, 1, 1}); }
            return 0;
        }

        std::string source;
        std::string name;
        bool echo = false;
        if (!script_path.empty()) { source = read_file(script_path); name = script_path; }
        else if (!eval_source.empty()) { source = eval_source; name = "<eval>"; echo = true; }
        else {
            source = platform_raw_command_line_after(static_cast<size_t>(first_one_shot_arg));
            name = "<command-line>"; echo = true;
        }
        Runtime runtime(quiet);
        try { check_and_execute(runtime, source, name, echo); }
        catch (const IncompleteInput&) { throw DiagnosticError("unexpected end of input", {static_cast<uint32_t>(source.size()), 0, 1, 1}); }
        return 0;
    } catch (const DiagnosticError& e) {
        std::fprintf(stderr, "error: %s (line %u, column %u)\n", e.what(), e.span.line, e.span.column);
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
