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
bool g_quiet = true;
bool g_assert_failed = false;
bool g_in_a_loop = false;

void print(const char* format, ...) {
    if (g_quiet) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

template <>
struct std::formatter<Token> : std::formatter<std::string_view> {
    auto format(Token c, format_context& ctx) const {
        std::string_view name = "Unknown";
        switch (c.type) {
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
        return std::formatter<std::string_view>::format(name, ctx);
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
void var_set(const std::string& name, Value val) {
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
        if (strcmp(argv[i], "--help") == 0) {
            help = true;
            g_interactive = false;
            g_quiet = false; // just in case
            script_file = NULL;
            break;
        } else if (strcmp(argv[i], "--script") == 0) {
            if (g_interactive) {
                std::println(stderr, "Cannot mix --interactive and --script");
                return 1;
            }
            if (i + 1 < argc) {
                script_file = argv[i + 1];
                i++;
            } else {
                std::println(stderr, "Error: --script requires a file path.");
                return 1;
            }
        } else if (strcmp(argv[i], "--interactive") == 0) {
            g_interactive = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            g_quiet = false;
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
                std::vector<Token> tokens = tokenize(buffer.c_str());
                parse_and_execute(tokens);
                
                buffer.clear();
                incomplete = false;

            } catch (const IncompleteInput&) {
                incomplete = true;
            } catch (const SyntaxError& e) {
                // In REPL, line 1 is usually the immediate line, so strictly speaking
                // printing line numbers might be redundant unless it's a multiline block.
                if (e.line > 0) std::println("Error (Line {}): {}", e.line, e.what());
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
            std::vector<Token> tokens = tokenize(content.c_str());
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
            std::print(
                "wilczurski's cool shit - ffi\n"
                "Basic usage: invoke.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...) [--print-result] [--assert[v]=<type>]\n"
                "    <func_name> can be an ordinal like #<ordinal>\n"
                "    or: invoke.exe --interactive or invoke.exe --script <script_path>\n"
                "    <type> can be: zero, nonzero, negative, nonnegative\n"
                "    assertv reports success/failure where assert exits if failure\n"
                "    The exception to this is usage in loops, there it is used as an exit condition\n"
                "    Scripts by default use .ffi, it is not enforced\n"
                "Usage in interactive/script mode is the same as non-interactive with more features\n"
                "    except when focused on a DLL, then you don't need to specify <dll_path>\n"
                "Commands in interactive/script mode:\n"
                "    To write a comment use ; like in assembly\n"
                "    /loaddll <path>                     Load and focus a DLL or just focus if loaded\n"
                "    /freedll <path>                     Unload a DLL\n"
                "    /set     <addr>     <type>  <value> Store a value at a memory address\n"
                "    /get     <addr>     <type>          Get a value from a memory address\n"
                "    /hex     <addr>     [count]         Hex dump memory\n"
                "    /address <dll_path> <name>          Get a function pointer by name or #ordinal\n"
                "    /struct  {{ <type> <name>, ... }}     Calculate the offsets and size of a struct\n"
                "    /struct  {{ $<name> = <type> <name>, ... }} Calculate the offsets and size of a struct and assign offsets\n"
                "    Both assume default packing and return the size. You can have structs in structs\n"
                "    /dlls                               List loaded DLLs\n"
                "    /for     <count>    {{<cmd>, ...}}    Repeat {{}} <count> times\n"
                "    /repeat             {{<cmd>, ...}}    Repeat {{}} until assert failure\n"
                "    /quit                               Exit the program\n"
                "Variables have no scopes and should not be modified by any callee, for that use malloc\n"
                "    $<name> = <type> <value>    Set variable value (e.g. $val = i32 10)\n"
                "    $<name> = <command>         Capture command/function output into variable\n"
                "    &$<name>                    Address-of: Get the memory pointer to a variable's storage\n"
                "    *$<name>                    Dereference: Read 64-bit value from the address stored in $<name>\n"
                "    $i is a reserved variable for loop iterations. It is intentionally not reset on break\n"
                "    Variables can be used as function arguments, like msvcrt.dll i32 printf(str \"%d\", i32 $<name>)\n"
                "    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'\n"
                "Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void\n"
                "    Or their \"proper\" version: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double\n"
                "    str, wstr, voidptr are equivalent to C's narrow null-terminated string (char*), wide string (wchar_t*), pointer (void*)\n"
                "    In the case of 'str' interpretation is entirely up to the callee (ACP, UTF-8, ASCII, or raw even bytes)."
                "    No validation or conversion is performed\n"
                "You can pass hex and decimal values. Types are advisory, not enforced. It's your fault when a function reads garbage\n"
                "SEH exists only to stop instant termination, not to save you. You are saved from null pointers in the built-in commands\n"
                "Any error is fatal when running a script\n"
            );
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
