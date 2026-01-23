#include "lexer.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMIXMAN
#include <windows.h>
#undef FAR
#undef NEAR
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <print>
#include <format>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include "common.hpp"

std::unordered_map<std::string, Value> vars;

bool g_interactive = false;
bool g_quiet = false;

const std::string help_string = R"(
wilczurski's cool shit - ffi
Basic usage: invoke.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...)
        [--print-result] [--assert[v]=<type>] [--quiet]
    <func_name> can be an ordinal like #<ordinal>
    or: invoke.exe --interactive or invoke.exe --script <script_path>
    <type> can be: zero, nonzero, negative, nonnegative
    assertv reports success/failure where assert exits if failure
    The exception to this is usage in loops, there it is used as an exit condition
    --quiet does not emit any prints to the console except errors. Also silences all struct definitions.
    Scripts by default use .ffi, it is not enforced
Usage in interactive/script mode is the same as non-interactive with more features
    except when focused on a DLL, then you don't need to specify <dll_path>
Commands in interactive/script mode:
    To write a comment use ; like in assembly
    /loaddll <path>                     Load and focus a DLL or just focus if loaded
    /freedll <path>                     Unload a DLL
    /set     <addr>     <type>  <value> Store a value at a memory address
    /get     <addr>     <type>          Get a value from a memory address
    /hex     <addr>     [count]         Hex dump memory
    /address <dll_path> <name>          Get a function pointer by name or #ordinal
    /struct  { <type> <name>, ... }     Calculate the offsets and size of a struct
    /struct  { $<name> = <type> <name>, ... } Calculate the offsets and size of a struct and assign offsets
    Both assume default packing and return the size. You can have structs in structs.
    Use the --quiet flag to suppress debug output. Local to this declaration
    /dlls                               List loaded DLLs
    /for     <count>    {<cmd>, ...}    Repeat {} <count> times. You can use this as an if you're stubborn enough
    /repeat             {<cmd>, ...}    Repeat {} until assert failure
    /quit                               Exit the program
Variables have no scopes and should not be modified by any callee, for that use malloc
    $<name> = <type> <value>    Set variable value (e.g. $val = i32 10)
    $<name> = <command>         Capture command/function output into variable
    &$<name>                    Address-of: Get the memory pointer to a variable's storage
    *$<name>                    Dereference: Read 64-bit value from the address stored in $<name>
    $i is a reserved variable for loop iterations. It is intentionally not reset on break
    Variables can be used as function arguments, like msvcrt.dll i32 printf(str "%d", i32 $<name>)
    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'
    you can have complex inline expressions like '$file_exists_bool = i64 (($or_i >> 63) | -($or_i >> 63)) & 1'
Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void
    Or their "proper" version: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double
    str, wstr, voidptr are equivalent to C's narrow null-terminated string (char*), wide string (wchar_t*), pointer (void*)
    In the case of 'str' interpretation is entirely up to the callee (ACP, UTF-8, ASCII, or raw even bytes).
    No validation or conversion is performed
You can pass hex and decimal values. Types are advisory, not enforced. It's your fault when a function reads garbage
SEH exists only to stop instant termination, not to save you. You are saved from null pointers in the built-in commands
Any error is fatal when running a script
)";

void print(const char* format, ...) {
    if (!g_quiet) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

template <>
struct std::formatter<Token> : std::formatter<std::string_view> {
    auto format(Token t, format_context& ctx) const {
        std::string_view name = "Unknown";
        switch (t.type) {
            case TokenType::TOK_EOF: name = "EOF"; break;
            case TokenType::TOK_IDENTIFIER: name = "TOK_IDENTIFIER"; break;
            case TokenType::TOK_INTEGER: name = "TOK_INTEGER"; break;
            case TokenType::TOK_FLOAT: name = "TOK_FLOAT"; break;
            case TokenType::TOK_STRING: name = "TOK_STRING"; break;
            case TokenType::TOK_RAW_STRING: name = "TOK_RAW_STRING"; break;
            case TokenType::TOK_VARIABLE: name = "TOK_VARIABLE"; break;
            case TokenType::TOK_FLAG: name = "TOK_FLAG"; break;
            case TokenType::TOK_LPAREN: name = "TOK_LPAREN"; break;
            case TokenType::TOK_RPAREN: name = "TOK_RPAREN"; break;
            case TokenType::TOK_LBRACE: name = "TOK_LBRACE"; break;
            case TokenType::TOK_RBRACE: name = "TOK_RBRACE"; break;
            case TokenType::TOK_COMMA: name = "TOK_COMMA"; break;
            case TokenType::TOK_EQUALS: name = "TOK_EQUALS"; break;
            case TokenType::TOK_AMP: name = "TOK_AMP"; break;
            case TokenType::TOK_PLUS: name = "TOK_PLUS"; break;
            case TokenType::TOK_MINUS: name = "TOK_MINUS"; break;
            case TokenType::TOK_STAR: name = "TOK_STAR"; break;
            case TokenType::TOK_SLASH: name = "TOK_SLASH"; break;
        }
        // We use std::format_to to build a detailed string representation
        if (t.type == TokenType::TOK_INTEGER) {
            return std::format_to(ctx.out(), "[{}: \"{}\" (val: {}) at line {}]", 
                                 name, t.text, t.int_val, t.line);
        }
        
        return std::format_to(ctx.out(), "[{}: \"{}\" at line {}]", 
                             name, t.text, t.line);
    }
};

// Find a variable by name, returns NULL if not found
bool var_get(const std::string& name, Value* out_val) {
    if (vars.find(name) != vars.end()) {
        *out_val = vars[name];
        return true;
    }
    return false;
}

// Returns the memory address of the storage for 'name'
// Used for &$var (passing pointer-to-variable)
uint64_t var_get_addr(const std::string& name) {
    // Note: Unordered_map pointers are unstable if rehashed!
    // But for a single FFI call, it should be safe.
    if (vars.find(name) == vars.end()) {
        vars[name] = {TypeKind::TYPE_PTR, 0}; // Create if not exists (Zero init)
    }
    return (uint64_t)&vars[name].value;
}

// Set a variable
enum class TypeCategory {
    CAT_INTEGER, CAT_FLOAT, CAT_STRING, CAT_POINTER, CAT_VOID, CAT_OTHER
};

TypeCategory get_category(TypeKind kind) {
    switch (kind) {
        case TypeKind::TYPE_I8:  case TypeKind::TYPE_I16: 
        case TypeKind::TYPE_I32: case TypeKind::TYPE_I64:
        case TypeKind::TYPE_U8:  case TypeKind::TYPE_U16: 
        case TypeKind::TYPE_U32: case TypeKind::TYPE_U64:
            return TypeCategory::CAT_INTEGER;
            
        case TypeKind::TYPE_F32: case TypeKind::TYPE_F64:
            return TypeCategory::CAT_FLOAT;
            
        case TypeKind::TYPE_STR: case TypeKind::TYPE_WSTR:
            return TypeCategory::CAT_STRING;
            
        case TypeKind::TYPE_PTR:
            return TypeCategory::CAT_POINTER;
            
        case TypeKind::TYPE_VOID:
            return TypeCategory::CAT_VOID;
            
        default:
            return TypeCategory::CAT_OTHER;
    }
}

void var_set(const std::string& name, Value val) {
    auto it = vars.find(name);

    if (it != vars.end()) {
        Value& old = it->second;
        
        // Skip check if the variable was previously "untyped" (VOID)
        if (old.type != TypeKind::TYPE_VOID) {
            // Compare categories rather than raw enum values
            if (get_category(val.type) != get_category(old.type)) {
                throw SyntaxError("Fundamental type mismatch: Cannot assign " + 
                                   std::to_string((int)val.type) + " to category " + 
                                   std::to_string((int)get_category(old.type)));
            }
        }
    }
    vars[name] = val;
}

bool var_exists(const std::string& name) {
    return vars.find(name) != vars.end();
}

int main(int argc, char** argv) {
    bool help = false;
    const char* script_file = NULL;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];

        if (arg == "--help") {
            help = true;
            g_interactive = false;
            g_quiet = false;
            script_file = nullptr;
            break; // Exit parsing early for help
        } 
        else if (arg == "--script") {
            if (g_interactive) {
                std::println("Error: Cannot mix --interactive and --script\n");
                return 1;
            }
            if (i + 1 < argc) {
                script_file = argv[++i]; // Increment i to skip the filename
            } else {
                std::println("Error: --script requires a file path.\n");
                return 1;
            }
        } 
        else if (arg == "--interactive") {
            if (script_file != nullptr) {
                std::println("Error: Cannot mix --interactive and --script\n");
                return 1;
            }
            g_interactive = true;
        } 
        else if (arg == "--quiet") {
            g_quiet = true;
        } 
    }

    if (g_interactive) {
        std::println("wilczurski's cool shit - repl");
        std::println("Enter command or /quit to exit.");
        
        std::string buffer;
        bool incomplete = false;

        while (true) {
            // Dynamic Prompt
            std::print("{}", incomplete ? "... " : "> ");
            std::fflush(stdout);
            
            std::string line;
            if (!std::getline(std::cin, line)) break; // EOF

            if (!buffer.empty()) buffer += "\n";
            buffer += line;
            
            try {
                std::vector<Token> tokens = tokenize(buffer);
                parse_and_execute(tokens);
                
                buffer.clear();
                incomplete = false;

            } catch (const IncompleteInput&) {
                incomplete = true;
            } catch (const SyntaxError& e) {
                // In REPL, line 1 is usually the immediate line, so strictly speaking
                // printing line numbers might be redundant unless it's a multiline block.
                if (e.line > 1) std::println("Error (Line {}): {}", e.line, e.what());
                else std::println("Error: {}", e.what());
                buffer.clear();
                incomplete = false;
            } catch (const std::exception& e) {
                std::println("System Error: {}", e.what());
                buffer.clear();
                incomplete = false;
            }
        }
    }
    else if (script_file) {
        print("wilczurski's cool shit - script: %s\n", script_file);

        // Use an input file stream
        std::ifstream file(script_file, std::ios::binary);
        
        if (!file) {
            std::println(stderr, "Failed to open script file: {}", script_file);
            return 1;
        }

        // Read the entire file into a std::string efficiently
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        try {
            std::vector<Token> tokens = tokenize(content);
            //for (auto& token : tokens) std::println("{}", token);
            parse_and_execute(tokens);
        } catch (const IncompleteInput&) {
            std::println(stderr, "Error: Script ended unexpectedly");
            return 1;
        } catch (const SyntaxError& e) {
            std::println(stderr, "Script Error at line {}: {}", e.line, e.what());
            return 1;
        } catch (const std::exception& e) {
            std::println(stderr, "Script Error: {}", e.what());
            return 1;
        }
    }
    else {
        if (argc < 2 || help) {
            std::print("{}", help_string.substr(1));
            return 1;
        }
        print("wilczurski's cool shit - one-shot\n");

        // Reconstruct the command line excluding the program name for the parser
        char* p = GetCommandLineA();
        
        // Skip executable name safely
        bool in_quote = false;
        while (*p) {
            if (*p == '"') in_quote = !in_quote;
            else if (*p == ' ' && !in_quote) {
                p++; 
                break;
            }
            p++;
        }
        
        try {
            std::vector<Token> tokens = tokenize(p);
            parse_and_execute(tokens);
        } catch (const SyntaxError& e) {
            // One-shot doesn't have relevant line numbers, ignore them
            std::println("Error: {}", e.what());
            return 1;
        } catch (const std::exception& e) {
            std::println("Error: {}", e.what());
            return 1;
        }
    }

    return 0;
}
