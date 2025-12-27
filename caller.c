// caller.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern uint64_t call_dynamic_function(void* func_ptr, uint64_t* args, int arg_count, uint32_t float_mask);

typedef enum {
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_STR, TYPE_VOIDPTR, TYPE_VOID
} ArgType;

typedef struct {
    ArgType type;
    uint64_t value;
} Argument;

typedef struct {
    char dll_path[256];
    char func_name[128];
    ArgType return_type;
    Argument args[16];
    int arg_count;
    int print_result;
} CallSpec;

// ------------------------ Helpers ------------------------
ArgType parse_type(const char* str) {
    if(strcmp(str,"i8")==0) return TYPE_I8;
    if(strcmp(str,"i16")==0) return TYPE_I16;
    if(strcmp(str,"i32")==0) return TYPE_I32;
    if(strcmp(str,"i64")==0) return TYPE_I64;
    if(strcmp(str,"u8")==0) return TYPE_U8;
    if(strcmp(str,"u16")==0) return TYPE_U16;
    if(strcmp(str,"u32")==0) return TYPE_U32;
    if(strcmp(str,"u64")==0) return TYPE_U64;
    if(strcmp(str,"f32")==0) return TYPE_F32;
    if(strcmp(str,"f64")==0) return TYPE_F64;
    if(strcmp(str,"str")==0) return TYPE_STR;
    if(strcmp(str,"voidptr")==0) return TYPE_VOIDPTR;
    if(strcmp(str,"void")==0) return TYPE_VOID;
    return TYPE_VOID;
}

uint64_t parse_argument_value(ArgType type, const char* str) {
    switch(type){
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
            return (uint64_t)atoll(str);
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
            return (uint64_t)strtoull(str,NULL,10);
        case TYPE_F32: {
            float f_val = (float)atof(str); // Parse to float
            double d_val = (double)f_val;   // Promote float to double
            uint64_t u64_bits;
            memcpy(&u64_bits, &d_val, sizeof(double)); // Get 64-bit pattern of the double
            return u64_bits;
        }
        case TYPE_F64: {
            double d_val = atof(str); // Parse directly to double
            uint64_t u64_bits;
            memcpy(&u64_bits, &d_val, sizeof(double)); // Get 64-bit pattern of the double
            return u64_bits;
        }
        case TYPE_STR: {
            int len = (int)strlen(str);
            char* src = (char*)str;
            int start = 0, end = len;

            // Remove quotes if present
            if(len >= 2 && str[0] == '"' && str[len-1] == '"') {
                start = 1;
                end = len - 1;
            }

            char* dest = malloc(end - start + 1);
            int j = 0;
            for (int i = start; i < end; i++) {
                if (src[i] == '\\' && i + 1 < end) {
                    switch (src[i + 1]) {
                        case 'n': dest[j++] = '\n'; i++; break;
                        case 'r': dest[j++] = '\r'; i++; break;
                        case 't': dest[j++] = '\t'; i++; break;
                        case '\\': dest[j++] = '\\'; i++; break;
                        case '\"': dest[j++] = '\"'; i++; break;
                        default:  dest[j++] = src[i]; break; // Keep as literal if unknown
                    }
                } else {
                    dest[j++] = src[i];
                }
            }
            dest[j] = '\0';
            return (uint64_t)dest;
        }
        case TYPE_VOIDPTR: return (uint64_t)strtoull(str,NULL,16);
        default: return 0;
    }
}

void format_result(uint64_t result, ArgType type, char* buf, size_t size){
    switch(type){
        case TYPE_I8: snprintf(buf,size,"%d",(int8_t)result); break;
        case TYPE_I16: snprintf(buf,size,"%d",(int16_t)result); break;
        case TYPE_I32: snprintf(buf,size,"%d",(int32_t)result); break;
        case TYPE_I64: snprintf(buf,size,"%lld",(int64_t)result); break;
        case TYPE_U8: snprintf(buf,size,"%u",(uint8_t)result); break;
        case TYPE_U16: snprintf(buf,size,"%u",(uint16_t)result); break;
        case TYPE_U32: snprintf(buf,size,"%u",(uint32_t)result); break;
        case TYPE_U64: snprintf(buf,size,"%llu",(uint64_t)result); break;
        case TYPE_F32: { float f; memcpy(&f,&result,sizeof(float)); snprintf(buf,size,"%f",f); break; }
        case TYPE_F64: { double d; memcpy(&d,&result,sizeof(double)); snprintf(buf,size,"%f",d); break; }
        case TYPE_STR: snprintf(buf,size,"%s",(char*)result); break;
        case TYPE_VOIDPTR: snprintf(buf,size,"0x%llx",result); break;
        case TYPE_VOID: snprintf(buf,size,"(void)"); break;
    }
}

// ------------------------ Command line parsing ------------------------
char* skip_ws(char* s){ while(*s==' '||*s=='\t') s++; return s; }

char* read_token(char** s){
    char* p = skip_ws(*s);
    if(!*p) return NULL;
    char* start = p;
    char* token;

    if(*p=='"'){ // quoted string
        p++;
        while(*p && *p!='"') p++;
        if(*p=='"') p++;
    } else {
        while(*p && *p!=' ' && *p!=',' && *p!=')') p++;
    }

    int len = (int)(p-start);
    token = malloc(len+1);
    strncpy(token,start,len);
    token[len]=0;

    if(*p==',' || *p==')') p++;
    *s = skip_ws(p);
    return token;
}

int parse_header(char** s, char* ret_type, char* func_name){
    char* p = skip_ws(*s);
    char* start = p;

    while(*p && *p!=' ' && *p!='\t') p++;
    int len = (int)(p-start);
    strncpy(ret_type,start,len);
    ret_type[len]=0;

    p = skip_ws(p);
    if(!*p) return 0;

    start = p;
    while(*p && *p!='(' && *p!=' ' && *p!='\t') p++;
    len = (int)(p-start);
    strncpy(func_name,start,len);
    func_name[len]=0;

    if(*p=='(') p++;
    *s = skip_ws(p);
    return 1;
}

void parse_arguments(char** s, CallSpec* spec){
    while(*s && **s && **s!=')'){
        char* type_token = read_token(s);
        char* val_token = read_token(s);
        if(!type_token || !val_token) break;

        ArgType t = parse_type(type_token);
        spec->args[spec->arg_count].type = t;
        spec->args[spec->arg_count].value = parse_argument_value(t,val_token);
        spec->arg_count++;

        free(type_token);
        free(val_token);
    }
    if(**s==')') (*s)++;
}

void parse_flags(char* f_str, CallSpec* spec) {
    if (!f_str) return;

    char* p = f_str;
    while (*p) {
        // Skip whitespace and potential leading dashes if we are mid-string
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        if (strncmp(p, "--print-result", 14) == 0) {
            spec->print_result = 1;
            p += 14;
        } else {
            // Move to next potential flag
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
}

typedef struct {
    char path[256];
    HMODULE handle;
} RegisteredDLL;

RegisteredDLL g_registry[32];
int g_registry_count = 0;
int g_focus_idx = -1; // Index of the "active" DLL for shorthand calls

// Helper to find a DLL in our registry
HMODULE find_registered_dll(const char* path, int* out_idx) {
    for (int i = 0; i < g_registry_count; i++) {
        if (_stricmp(g_registry[i].path, path) == 0) {
            if (out_idx) *out_idx = i;
            return g_registry[i].handle;
        }
    }
    return NULL;
}

void process_command(char* input_line) {
    char* p = skip_ws(input_line);
    if (!*p) return;

    // --- Command: /quit ---
    if (strncmp(p, "/quit", 5) == 0) {
        for (int i = 0; i < g_registry_count; i++) FreeLibrary(g_registry[i].handle);
        exit(0);
    }

    // --- Command: /loaddll <path> ---
    if (strncmp(p, "/loaddll", 8) == 0) {
        p = skip_ws(p + 8);
        if (find_registered_dll(p, &g_focus_idx)) {
            printf("DLL '%s' is already loaded (Focus set).\n", p);
        } else if (g_registry_count < 32) {
            HMODULE h = LoadLibraryA(p);
            if (h) {
                strncpy(g_registry[g_registry_count].path, p, 255);
                g_registry[g_registry_count].handle = h;
                g_focus_idx = g_registry_count;
                g_registry_count++;
                printf("Loaded and registered: %s (Focus set)\n", p);
            } else {
                printf("Error: Could not load '%s' (Error: %lu)\n", p, GetLastError());
            }
        }
        return;
    }

    // --- Command: /freedll <path> ---
    if (strncmp(p, "/freedll", 8) == 0) {
        p = skip_ws(p + 8);
        int idx = -1;
        if (find_registered_dll(p, &idx)) {
            FreeLibrary(g_registry[idx].handle);
            // Shift registry down
            for (int i = idx; i < g_registry_count - 1; i++) g_registry[i] = g_registry[i+1];
            g_registry_count--;
            g_focus_idx = (g_registry_count > 0) ? 0 : -1;
            printf("Unloaded: %s\n", p);
        } else {
            printf("DLL '%s' not found in registry.\n", p);
        }
        return;
    }

    // --- Execution Logic ---
    CallSpec spec = {0};
    char* flags_ptr = NULL;
    int in_quotes = 0;
    for (char* s = p; *s; s++) {
        if (*s == '"') in_quotes = !in_quotes;
        if (!in_quotes && s[0] == '-' && s[1] == '-') { flags_ptr = s; break; }
    }
    if (flags_ptr) { parse_flags(flags_ptr, &spec); *flags_ptr = '\0'; }

    HMODULE target_dll = NULL;
    char first_token[256];
    
    // Peek at the first token to see if it's a DLL path or a return type
    char* temp_p = p;
    char* token = read_token(&temp_p);
    if (!token) return;
    strncpy(first_token, token, 255);
    free(token);

    // If first_token contains ".dll", it's a manual DLL call
    if (strstr(first_token, ".dll") != NULL) {
        target_dll = find_registered_dll(first_token, NULL);
        if (!target_dll) {
            target_dll = LoadLibraryA(first_token); // Temp load if not in registry
            if (!target_dll) {
                printf("Error: Failed to load %s\n", first_token);
                return;
            }
        }
        p = temp_p; // Advance past the DLL name
    } else {
        // Shorthand: use the focused DLL
        if (g_focus_idx != -1) {
            target_dll = g_registry[g_focus_idx].handle;
        } else {
            printf("Error: No DLL focused. Use /loaddll or specify <name>.dll\n");
            return;
        }
    }

    // Parse Header (ret_type func_name)
    char ret_type_str[32];
    if (!parse_header(&p, ret_type_str, spec.func_name)) {
        printf("Error: Signature parse failed\n");
        return;
    }
    spec.return_type = parse_type(ret_type_str);
    parse_arguments(&p, &spec);

    // Call Function
    void* func_ptr = (void*)GetProcAddress(target_dll, spec.func_name);
    if (!func_ptr) {
        printf("Error: Function '%s' not found.\n", spec.func_name);
    } else {
        uint64_t args[16] = {0};
        uint32_t float_mask = 0;
        for (int j = 0; j < spec.arg_count; j++) {
            args[j] = spec.args[j].value;
            if (spec.args[j].type == TYPE_F32 || spec.args[j].type == TYPE_F64) float_mask |= (1 << j);
        }
        uint64_t result = call_dynamic_function(func_ptr, args, spec.arg_count, float_mask);
        if (spec.print_result && spec.return_type != TYPE_VOID) {
            char buf[256];
            format_result(result, spec.return_type, buf, sizeof(buf));
            printf("Result: %s\n", buf);
        }
    }

    // Cleanup string arguments
    for (int j = 0; j < spec.arg_count; j++) {
        if (spec.args[j].type == TYPE_STR) free((void*)spec.args[j].value);
    }
}

int main(int argc, char** argv) {
    int interactive = 0;
    
    // Check if the user wants interactive mode
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--interactive") == 0) {
            interactive = 1;
            break;
        }
    }

    if (interactive) {
        printf("--- Interactive DLL Caller ---\n");
        printf("Enter command or /quit to exit.\n");
        char line[1024];
        while (1) {
            printf("> ");
            if (!fgets(line, sizeof(line), stdin)) break;
            
            // Remove newline
            line[strcspn(line, "\n")] = 0;
            process_command(line);
        }
    } else {
        if (argc < 2) {
            printf("wilczurski's cool shit - ffi\n");
            printf("Usage: caller.exe <dll_path> <ret> <func>(<args>) [--print-result]\n");
            printf("   or: caller.exe --interactive\n");
            printf("   Available commands in interactive mode:\n");
            printf("     /loaddll <path>       Load and focus a DLL\n");
            printf("     /freedll <path>      Unload a DLL from registry\n");
            printf("     /quit                Exit the program\n");
            printf("    Usage in interactive mode is the same as non-interactive.\n");
            printf("    Types: i8, i16, i32, i64, u8, u16, u32, u64\n");
            printf("    equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t\n");
            printf("    f32, f64, str, voidptr, void\n");
            printf("    equivalent to float, double, null-terminated string, pointer (hex), and void\n");
            return 1;
        }

        // Reconstruct the command line excluding the program name for the parser
        char* cmdline_raw = GetCommandLineA();
        char* p = cmdline_raw;
        
        // Skip exe name
        if (*p == '"') {
            p++; while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ') p++;
        }
        
        process_command(_strdup(p));
    }

    return 0;
}
