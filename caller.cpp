#include "lexer.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "common.hpp"

RegisteredDLL g_registry[32];
int g_registry_count = 0;
int g_focus_idx = -1;

Struct g_known_structs[256];
int g_known_struct_count = 0;

Variable g_vars[128];
int g_var_count = 0;

bool g_interactive = false;
bool g_normal_vars = false;
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

// Find a variable by name, returns NULL if not found
Variable* find_var(const char* name) {
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            return &g_vars[i];
        }
    }
    return NULL;
}

// Set a variable (only if not already set)
bool set_var(const char* name, uint64_t value) {
    Variable* existing = find_var(name);
    if (existing) {
        existing->value = value;
        return true;
    }
    
    if (g_var_count >= 128) {
        printf("Error: Maximum number of variables reached\n");
        if (!g_interactive)
            exit(1);
        return false;
    }
    
    strncpy(g_vars[g_var_count].name, name, 63);
    g_vars[g_var_count].name[63] = '\0';
    g_vars[g_var_count].value = value;
    g_var_count++;
    return true;
}

int main(int argc, char** argv) {
    bool help = false;
    const char* script_file = NULL;
    set_var("i", 0);

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            help = true;
            g_interactive = false;
            script_file = NULL;
            break;
        }
        else if (strcmp(argv[i], "--interactive") == 0) {
            g_interactive = true;
            if (i + 1 < argc && strcmp(argv[i + 1], "--normal-variables-pretty-please") == 0) {
                g_normal_vars = true;
                i++;
            }
        } else if (strcmp(argv[i], "--script") == 0) {
            if (i + 1 < argc) {
                script_file = argv[i + 1];
                i++;
            } else {
                fprintf(stderr, "Error: --script requires a file path.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--quiet") == 0) {
            g_quiet = false;
        }
    }

    if (g_interactive) {
        printf("wilczurski's cool shit - repl\n");
        printf("Enter command or /quit to exit.\n");
        char line[1024];
        while (1) {
            printf("> ");
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\n")] = 0; // remove newline
            process_command(line);
        }
    } else if (script_file) {
        print("wilczurski's cool shit - script\n");
        FILE* f = fopen(script_file, "r");
        if (!f) {
            fprintf(stderr, "Failed to open script file: %s\n", script_file);
            return 1;
        }

        // 1. Determine file size
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        // 2. Allocate memory and read the entire file
        char *content = (char*)malloc(fsize + 1);
        if (!content) {
            fprintf(stderr, "Memory allocation failed\n");
            fclose(f);
            return 1;
        }
        memset(content, 0, fsize);
        fread(content, 1, fsize, f);
        content[fsize] = 0; // Null-terminate
        fclose(f);

        for (int i = 0; i < fsize - 1; i++) {
            if (content[i] == '^' && (content[i+1] == '\n' || content[i+1] == '\r')) {
                content[i] = ' ';   // Replace '^' with space
                content[i+1] = ' '; // Replace '\n' or '\r' with space
                
                // Handle Windows CRLF (\r\n)
                if (i + 2 < fsize && content[i+1] == '\r' && content[i+2] == '\n') {
                    content[i+2] = ' ';
                }
            }
        }

        // 3. Process commands from the buffer
        char *line = strtok(content, "\n");
        while (line != NULL) {
            // line is already null-terminated at the newline by strtok
            //print("> %s\n", line);
            //process_command(line);
            line = strtok(NULL, "\n");
            //if (g_assert_failed)
                //break;
            std::vector<Token> t = tokenize(line);
            for(auto& tok : t) {
                printf("Type: %d | Text: %s\n", tok.type, tok.text.c_str());
            }
            if (strcmp(line, "/quit") == 0)
                break;
        }

        free(content);
    } else {
        if (argc < 2 || help) {
            printf(
                "wilczurski's cool shit - repl + ffi\n"
                "Usage: caller.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...) [--print-result] [--assert=<type>]\n"
                "    <func_name> can be an ordinal #<ordinal>\n"
                "    or: caller.exe --interactive [--normal-variables-pretty-please] or caller.exe --script <script_path>\n"
                "    Scripts by default use .ffi\n"
                "    <type> can be: zero, nonzero, negative, nonnegative, not specifying means none\n"
                "Usage in interactive/script mode is the same as non-interactive except when focused on a DLL, then you don't need to specify <dll_path>\n"
                "Additional usage in interactive/script mode:\n"
                "    To write a comment use ; like assembly\n"
                "    /enable-normal-variables-pretty-please Enables \"normal variables\" in scripts or interactive mode if you forgot to add the flag\n"
                "    /loaddll <path>                     Load and focus a DLL, not required mind you\n"
                "    /freedll <path>                     Unload a DLL from registry\n"
                "    /alloc   <size>                     Allocate memory. Provide <size> in bytes\n"
                "    /free    <addr>                     Free allocated memory\n"
                "    /set     <addr>     <type>  <value> Store a value at a memory address\n"
                "    /memset  <addr>     <value> [count] Set a block of memory to a byte value\n"
                "    /get     <addr>     <type>          Get a value from a memory address\n"
                "    /hex     <addr>     [count]         Hex dump memory (default 64 bytes)\n"
                "    /address <dll_path> <name>          Get a function pointer by name\n"
                "    /struct  { <type> <name>, ... }     Calculate the offsets and size of a struct\n"
                "    /struct  { $<name> = <type> <name>, ... } Calculate the offsets and size of a struct and assign them\n"
                "    /dlls                               List loaded DLLs\n"
                "    /for     <count>    {<cmd>, ...}    Repeat {} <count> times\n"
                "    /repeat-until       {<cmd>, ...}    Repeat {} until assert\n"
                "    /quit                               Exit the program\n"
                "Variables by default are Write-Once Read-Many, no shadowing, no scopes.\n"
                "    --normal-variables-pretty-please allows reassignment. Not recommended.\n"
                "    $<name> = <type> <value>    Set variable value (e.g. $val = i32 10)\n"
                "    $<name> = <command>         Capture command/function output into variable\n"
                "    &$<name>                    Address-of: Get the memory pointer to a variable's storage\n"
                "    *$<name>                    Dereference: Read 64-bit value from the address stored in $<name>\n"
                "    $i is a reserved variable for loop iterations. It is intentionally not reset on break\n"
                "    Variables can be used as function arguments, like test.dll void print(str \"%%d\", i32 $var1)\n"
                "    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'\n"
                "Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void\n"
                "    Or their \"proper\" version: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double,\n"
                "    str, wstr, voidptr are equivelant to C's \"narrow\" null-terminated string (char*), wide string (wchar_t*), pointer (void*, always hex)\n"
                "    In the case of 'str' interpretation is entirely up to the callee (ACP, UTF-8, ASCII, or raw bytes). No validation or conversion is performed.\n"
                "You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type (except pointers).\n"
                "SEH is there to help, but continue at your own risk. In scripts any error is fatal and will exit.\n"
            );
            return 1;
        }
        print("wilczurski's cool shit - one-shot\n");

        // Reconstruct the command line excluding the program name for the parser
        char* p = GetCommandLineA();
        
        // Skip exe name
        if (*p == '"') {
            p++; while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ') p++;
        }
        
        process_command(strdup(p));
    }

    return 0;
}
