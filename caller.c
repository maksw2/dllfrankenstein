// caller.c
#include <windows.h>
#include <stdbool.h>
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

typedef enum {
    ASSERT_NONE,
    ASSERT_ZERO,
    ASSERT_NOT_ZERO,
    ASSERT_NEGATIVE,
    ASSERT_NON_NEGATIVE
} AssertType;

typedef struct {
    ArgType type;
    uint64_t value;
} Argument;

typedef struct {
    char dll_path[256];
    char func_name[128];
    ArgType return_type;
    AssertType assert_type;
    Argument args[16];
    int arg_count;
    int print_result;
} CallSpec;

typedef struct {
    char name[64];
    uint64_t value;
    bool is_set;
} Variable;

Variable g_vars[128];
int g_var_count = 0;

bool interactive = false;
bool normal_vars = false;

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
        if (existing->is_set && !normal_vars) {
            printf("Error: Variable '%s' is already set\n", name);
            return false;
        }
        existing->value = value;
        existing->is_set = true;
        return true;
    }
    
    if (g_var_count >= 128) {
        printf("Error: Maximum number of variables reached\n");
        return false;
    }
    
    strncpy(g_vars[g_var_count].name, name, 63);
    g_vars[g_var_count].name[63] = '\0';
    g_vars[g_var_count].value = value;
    g_vars[g_var_count].is_set = true;
    g_var_count++;
    return true;
}

char* skip_ws(char* s) {
    while(*s==' '||*s=='\t') 
        s++;
    return s;
}

char* skip_ws_backwards(char* str, char* limit) {
    char* p = str;
    while (p > limit && isspace((unsigned char)*(p - 1)))
        p--;
    return p;
}

char* expand_vars(const char* input) {
    char* current_str = _strdup(input);
    bool changed = true;

    while (changed) {
        changed = false;
        char* dollar = strchr(current_str, '$');
        if (!dollar) break;

        // 1. Extract variable name
        char* name_start = dollar + 1;
        char* name_end = name_start;
        while (*name_end && (isalnum((unsigned char)*name_end) || *name_end == '_')) {
            name_end++;
        }

        int name_len = (int)(name_end - name_start);
        if (name_len == 0) break; 

        char var_name[64];
        strncpy(var_name, name_start, name_len);
        var_name[name_len] = '\0';

        Variable* var = find_var(var_name);
        if (!var) {
            printf("Error: Variable '$%s' not found\n", var_name);
            break; 
        }

        uint64_t final_val = var->value;
        char* expression_end = name_end;

        // 2. Check for arithmetic suffix (e.g., + 0x08 or - 10)
        char* p = skip_ws(name_end);
        if (*p == '+' || *p == '-') {
            char op = *p;
            char* num_ptr = skip_ws(p + 1);
            if (isxdigit((unsigned char)*num_ptr)) {
                char* endptr;
                uint64_t offset = strtoull(num_ptr, &endptr, 0);
                if (op == '+') final_val += offset;
                else final_val -= offset;
                expression_end = endptr; // Consume the math part of the string
            }
        }

        // 3. Construct the replacement hex string
        char val_buf[32];
        sprintf(val_buf, "0x%llX", final_val);

        size_t prefix_len = dollar - current_str;
        size_t suffix_len = strlen(expression_end);
        size_t val_len = strlen(val_buf);
        
        char* new_str = malloc(prefix_len + val_len + suffix_len + 1);
        memcpy(new_str, current_str, prefix_len);
        memcpy(new_str + prefix_len, val_buf, val_len);
        memcpy(new_str + prefix_len + val_len, expression_end, suffix_len + 1);

        free(current_str);
        current_str = new_str;
        changed = true; 
    }

    return current_str;
}

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
            return (uint64_t)strtoll(str,NULL,0);
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
            return (uint64_t)strtoull(str,NULL,0);
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
        case TYPE_VOIDPTR: return strtoull(str,NULL,16);
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
        case TYPE_VOIDPTR: snprintf(buf,size,"0x%llX",result); break;
        case TYPE_VOID: snprintf(buf,size,"(void)"); break;
    }
}

// ------------------------ Command line parsing ------------------------
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
        } else if (strncmp(p, "--assert=zero", 13) == 0) {
            spec->assert_type = ASSERT_ZERO;
            p += 13;
        } else if (strncmp(p, "--assert=nonzero", 16) == 0) {
            spec->assert_type = ASSERT_NOT_ZERO;
            p += 16;
        } else if (strncmp(p, "--assert=negative", 16) == 0) {
            spec->assert_type = ASSERT_NEGATIVE;
            p += 16;
        } else if (strncmp(p, "--assert=nonnegative", 20) == 0) {
            spec->assert_type = ASSERT_NON_NEGATIVE;
            p += 20;
        } else if (strncmp(p, "--assert", 8) == 0) {
            spec->assert_type = ASSERT_ZERO;
            p += 8;
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

void handle_quit_command() {
    for (int i = 0; i < g_registry_count; i++) FreeLibrary(g_registry[i].handle);
    exit(0);
}

uint64_t handle_alloc_command(char* p) {
    p = skip_ws(p + 6);
    char* expanded = expand_vars(p);
    int size = atoi(expanded);
    void* ptr = malloc(size);
    printf("0x%llX\n", (uint64_t)ptr);
    free(expanded);
    return (uint64_t)ptr; // Return the address
}

void handle_free_command(char* p) {
    p = skip_ws(p + 5);
    char* expanded = expand_vars(p);
    void* ptr = (void*)strtoull(expanded, NULL, 16);
    free(ptr);
    printf("Freed memory at 0x%llX\n", (uint64_t)ptr);
    free(expanded);
}

void handle_set_command(char* p) {
    p = skip_ws(p + 4);
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    
    char* addr_str = read_token(&work_p);
    char* type_str = read_token(&work_p);
    char* val_str  = read_token(&work_p);
    if (!addr_str || !type_str || !val_str) {
        free(expanded);
        return;
    }

    uint64_t addr = strtoull(addr_str, NULL, 16);
    ArgType type = parse_type(type_str);
    uint64_t value = parse_argument_value(type, val_str);

    switch(type){
        case TYPE_I8:  *(int8_t*)addr  = (int8_t)value; break;
        case TYPE_I16: *(int16_t*)addr = (int16_t)value; break;
        case TYPE_I32: *(int32_t*)addr = (int32_t)value; break;
        case TYPE_I64: *(int64_t*)addr = (int64_t)value; break;
        case TYPE_U8:  *(uint8_t*)addr  = (uint8_t)value; break;
        case TYPE_U16: *(uint16_t*)addr = (uint16_t)value; break;
        case TYPE_U32: *(uint32_t*)addr = (uint32_t)value; break;
        case TYPE_U64: *(uint64_t*)addr = (uint64_t)value; break;
        case TYPE_F32: { float f; memcpy(&f,&value,sizeof(float)); *(float*)addr = f; } break;
        case TYPE_F64: { double d; memcpy(&d,&value,sizeof(double)); *(double*)addr = d; } break;
        case TYPE_STR: 
            if (value != 0) strcpy((char*)addr, (char*)value);
            break;
        case TYPE_VOIDPTR: *(uint64_t*)addr = value; break;
        default: printf("unsupported\n"); break;
    }
    
    char res_buf[256];
    format_result(value, type, res_buf, sizeof(res_buf));
    printf("Value at 0x%llX (%s): %s\n", addr, type_str, res_buf);
    
    free(addr_str); free(type_str); free(val_str);
    free(expanded);
}

void handle_memset_command(char* p) {
    p = skip_ws(p + 7);
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    
    char* addr_str = read_token(&work_p);
    char* val_str = read_token(&work_p);
    char* count_str = read_token(&work_p);
    if (!addr_str || !val_str || !count_str) {
        free(expanded);
        return;
    }

    uint64_t addr = strtoull(addr_str, NULL, 16);
    uint8_t value = (uint8_t)strtoul(val_str, NULL, 10);
    size_t count = (size_t)strtoul(count_str, NULL, 10);

    memset((void*)addr, value, count);
    printf("Set %zu bytes at 0x%llX to 0x%02x\n", count, addr, value);

    free(addr_str); free(val_str); free(count_str);
    free(expanded);
}

uint64_t handle_get_command(char* p) {
    p = skip_ws(p + 4);
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    
    char* addr_str = read_token(&work_p);
    char* type_str = read_token(&work_p);
    if (!addr_str || !type_str) {
        free(expanded);
        return 0;
    }

    uint64_t addr = strtoull(addr_str, NULL, 16);
    ArgType type = parse_type(type_str);

    uint64_t value = 0;
    switch(type){
        case TYPE_I8:  value = (uint64_t)*(int8_t*)addr; break;
        case TYPE_I16: value = (uint64_t)*(int16_t*)addr; break;
        case TYPE_I32: value = (uint64_t)*(int32_t*)addr; break;
        case TYPE_I64: value = (uint64_t)*(int64_t*)addr; break;
        case TYPE_U8:  value = (uint64_t)*(uint8_t*)addr; break;
        case TYPE_U16: value = (uint64_t)*(uint16_t*)addr; break;
        case TYPE_U32: value = (uint64_t)*(uint32_t*)addr; break;
        case TYPE_U64: value = (uint64_t)*(uint64_t*)addr; break;
        case TYPE_F32: { float f = *(float*)addr; memcpy(&value,&f,sizeof(float)); } break;
        case TYPE_F64: { double d = *(double*)addr; memcpy(&value,&d,sizeof(double)); } break;
        case TYPE_STR: value = addr; break;
        case TYPE_VOIDPTR: value = *(uint64_t*)addr; break;
        default: printf("unsupported\n"); break;
    }
    
    char res_buf[256];
    format_result(value, type, res_buf, sizeof(res_buf));
    printf("Value at 0x%llX (%s): %s\n", addr, type_str, res_buf);
    
    free(addr_str); free(type_str); free(expanded);
    return value;
}

void handle_hex_command(char* p) {
    p = skip_ws(p + 4);
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    
    char* addr_str = read_token(&work_p);
    char* count_str = read_token(&work_p);
    if (!addr_str) {
        free(expanded);
        return;
    }

    uint64_t addr = strtoull(addr_str, NULL, 16);
    int count = count_str ? atoi(count_str) : 64;
    if (count <= 0) count = 16;

    unsigned char* data = (unsigned char*)addr;
    
    printf("Dump of 0x%llX (%d bytes):\n", (uint64_t)data, count);
    for (int i = 0; i < count; i += 16) {
        printf("  %llX: ", (uint64_t)(data + i));
        
        for (int j = 0; j < 16; j++) {
            if (i + j < count)
                printf("%02X ", data[i + j]);
            else
                printf("   ");
        }
        
        printf(" |");
        for (int j = 0; j < 16; j++) {
            if (i + j < count) {
                unsigned char c = data[i + j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        printf("|\n");
    }

    free(addr_str); 
    if(count_str) free(count_str);
    free(expanded);
}

uint64_t handle_address_command(char* p) {
    p = skip_ws(p + 8);
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    char* dll_path = read_token(&work_p);
    char* name_str = read_token(&work_p);
    
    uint64_t result_addr = 0;
    if (dll_path && name_str) {
        HMODULE h = find_registered_dll(dll_path, NULL);
        bool temp_load = false;
        if (!h) { h = LoadLibraryA(dll_path); temp_load = true; }

        if (h) {
            result_addr = (uint64_t)GetProcAddress(h, name_str);
            if (result_addr) printf("0x%llX\n", result_addr);
            if (temp_load) FreeLibrary(h);
        }
    }

    free(dll_path); free(name_str); free(expanded);
    return result_addr;
}

void handle_loaddll_command(char* p) {
    p = skip_ws(p + 8);
    char* expanded = expand_vars(p);

    if (find_registered_dll(expanded, &g_focus_idx)) {
        printf("DLL '%s' is already loaded (Focus set).\n", expanded);
    } else if (g_registry_count < 32) {
        HMODULE h = LoadLibraryA(expanded);
        if (h) {
            strncpy(g_registry[g_registry_count].path, expanded, 255);
            g_registry[g_registry_count].handle = h;
            g_focus_idx = g_registry_count;
            g_registry_count++;
            printf("Loaded and registered: %s (Focus set)\n", expanded);
        } else {
            printf("Error: Could not load '%s' (Error: %lu)\n", expanded, GetLastError());
        }
    }
    
    free(expanded);
}

void handle_freedll_command(char* p) {
    p = skip_ws(p + 8);
    char* expanded = expand_vars(p);

    int idx = -1;
    if (find_registered_dll(expanded, &idx)) {
        FreeLibrary(g_registry[idx].handle);
        for (int i = idx; i < g_registry_count - 1; i++) g_registry[i] = g_registry[i+1];
        g_registry_count--;
        g_focus_idx = (g_registry_count > 0) ? 0 : -1;
        printf("Unloaded: %s\n", expanded);
    } else {
        printf("DLL '%s' not found in registry.\n", expanded);
    }
    
    free(expanded);
}

uint64_t handle_function_call(char* input_line) {
    char* p = input_line;
    char* expanded = expand_vars(p);
    p = expanded;

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
    
    char* temp_p = p;
    char* token = read_token(&temp_p);
    if (!token) {
        free(expanded);
        return 0;
    }
    strncpy(first_token, token, 255);
    free(token);

    if (strstr(first_token, ".dll") != NULL) {
        target_dll = find_registered_dll(first_token, NULL);
        if (!target_dll) {
            target_dll = LoadLibraryA(first_token);
            if (!target_dll) {
                printf("Error: Failed to load %s\n", first_token);
                free(expanded);
                return 0;
            }
        }
        p = temp_p;
    } else {
        if (g_focus_idx != -1) {
            target_dll = g_registry[g_focus_idx].handle;
        } else {
            printf("Error: No DLL focused. Use /loaddll or specify <name>.dll\n");
            free(expanded);
            return 0;
        }
    }

    char ret_type_str[32];
    if (!parse_header(&p, ret_type_str, spec.func_name)) {
        printf("Error: Signature parse failed\n");
        free(expanded);
        return 0;
    }
    spec.return_type = parse_type(ret_type_str);
    parse_arguments(&p, &spec);

    void* func_ptr = NULL;

    // Check if the function name is an ordinal (starts with #)
    if (spec.func_name[0] == '#') {
        char* end = nullptr;
        long ordinal = strtol(spec.func_name + 1, &end, 10);

        if (end != spec.func_name + 1 && ordinal > 0 && ordinal <= 0xFFFF) {
            func_ptr = (void*)GetProcAddress(
                target_dll,
                MAKEINTRESOURCEA((WORD)ordinal)
            );
        }
    } else {
        func_ptr = (void*)GetProcAddress(target_dll, spec.func_name);
    }
    
    if (!func_ptr) {
        printf("Error: Function '%s' not found.\n", spec.func_name);

        for (int j = 0; j < spec.arg_count; j++) {
            if (spec.args[j].type == TYPE_STR) free((void*)spec.args[j].value);
        }
        
        free(expanded);
        return 0;
    }

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
    
    if (spec.assert_type != ASSERT_NONE) {
        bool assert_failed = false;
        switch (spec.assert_type) {
            case ASSERT_ZERO: if (result != 0) assert_failed = true; break;
            case ASSERT_NOT_ZERO: if (result == 0) assert_failed = true; break;
            case ASSERT_NEGATIVE: if ((int64_t)result >= 0) assert_failed = true; break;
            case ASSERT_NON_NEGATIVE: if ((int64_t)result < 0) assert_failed = true; break;
            default: break;
        }
        if (assert_failed) {
            printf("Assertion failed for result: ");
            char buf[256];
            format_result(result, spec.return_type, buf, sizeof(buf));
            printf("%s\n", buf);

            if (!interactive)
                exit(1);
        }
    }

    for (int j = 0; j < spec.arg_count; j++) {
        if (spec.args[j].type == TYPE_STR) free((void*)spec.args[j].value);
    }
    
    free(expanded);
    
    if (spec.return_type != TYPE_VOID) return result;
    return 0;
}

uint64_t process_command(char* input_line) {
    char* p = skip_ws(input_line);
    if (!*p) return 0;

    // 1. Handle Variable Assignment ($a = ...)
    if (*p == '$') {
        char* eq_ptr = strchr(p, '=');
        if (eq_ptr) {
            char var_name[64] = {0};
            char* name_start = p + 1;
            char* name_end = skip_ws_backwards(eq_ptr, name_start);
            size_t name_len = (size_t)(name_end - name_start);
            if (name_len > 63) name_len = 63;
            memcpy(var_name, name_start, name_len);
            var_name[name_len] = '\0';

            // Recursive call for the Right-Hand Side
            uint64_t result = process_command(skip_ws(eq_ptr + 1));

            if (set_var(var_name, result)) {
                printf("$%s = 0x%llX\n", var_name, result);
            }
            return result;
        }
    }

    // Command dispatch
    if (*p == '/') {
        if (strncmp(p, "/quit", 5) == 0)    { handle_quit_command(); return 0; }
        else if (strncmp(p, "/alloc", 6) == 0)   { return handle_alloc_command(p); }
        else if (strncmp(p, "/free", 5) == 0)    { handle_free_command(p); return 0; }
        else if (strncmp(p, "/set", 4) == 0)     { handle_set_command(p); return 0; }
        else if (strncmp(p, "/memset", 7) == 0)  { handle_memset_command(p); return 0; }
        else if (strncmp(p, "/get", 4) == 0)     { return handle_get_command(p); }
        else if (strncmp(p, "/hex", 4) == 0)     { handle_hex_command(p); return 0; }
        else if (strncmp(p, "/address", 8) == 0) { return handle_address_command(p); }
        else if (strncmp(p, "/loaddll", 8) == 0) { handle_loaddll_command(p); return 0; }
    }

    char* temp_p = p;
    char* first_token = read_token(&temp_p);
    if (first_token) {
        ArgType t = parse_type(first_token);
        
        // Only enter if there is a value
        if (t != TYPE_VOID) { 
            char* val_str = read_token(&temp_p);
            if (val_str) {
                uint64_t result = parse_argument_value(t, val_str);
                free(val_str);
                free(first_token);
                return result; 
            }
        }
        free(first_token);
    }
    
    // Default: treat as function call
    return handle_function_call(input_line);
}

int main(int argc, char** argv) {
    const char* script_file = NULL;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
            if (i + 1 < argc && strcmp(argv[i + 1], "--normal-variables-pretty-please") == 0) {
                normal_vars = true;
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
        }
    }

    if (interactive) {
        printf("--- Interactive DLL Caller ---\n");
        printf("Enter command or /quit to exit.\n");
        char line[1024];
        while (1) {
            printf("> ");
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\n")] = 0; // remove newline
            process_command(line);
        }
    } else if (script_file) {
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
        char *content = malloc(fsize + 1);
        if (!content) {
            fprintf(stderr, "Memory allocation failed\n");
            fclose(f);
            return 1;
        }
        fread(content, 1, fsize, f);
        content[fsize] = 0; // Null-terminate
        fclose(f);

        // 3. Process commands from the buffer
        char *line = strtok(content, "\n");
        while (line != NULL) {
            // line is already null-terminated at the newline by strtok
            printf("> %s\n", line);
            process_command(line);
            line = strtok(NULL, "\n");
        }

        free(content);
    } else {
        if (argc < 2) {
            printf(
                "wilczurski's cool shit - repl + ffi\n"
                "Usage: caller.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value, ...) [--print-result] [--assert=<type>]\n"
                "    <func_name> can be an ordinal #<ordinal>\n"
                "    or: caller.exe --interactive [--normal-variables-pretty-please] or caller.exe --script <script_path>\n"
                "    Scripts by default use .ffi\n"
                "    <type> can be: zero, nonzero, negative, nonnegative, not specifying means none\n"
                "Usage in interactive mode:\n"
                "    /loaddll <path>                     Load and focus a DLL\n"
                "    /freedll <path>                     Unload a DLL from registry\n"
                "    /alloc   <size>                     Allocate memory. Provide <size> in bytes\n"
                "    /free    <addr>                     Free allocated memory\n"
                "    /set     <addr>     <type>  <value> Store a value at a memory address\n"
                "    /memset  <addr>     <value> [count] Set a block of memory to a byte value\n"
                "    /get     <addr>     <type>          Get a value from a memory address\n"
                "    /hex     <addr>     [count]         Hex dump memory (default 64 bytes)\n"
                "    /address <dll_path> <name>          Get a function pointer by name\n"
                "    /quit                               Exit the program\n"
                "Variables by default are Write-Once Read-Many, no shadowing, no scopes.\n"
                "    --normal-variables-pretty-please allows reassignment. Not recommended.\n"
                "    $<name> = <type> <value>            Assign a variable\n"
                "    $<name> = rhs                       Function call or command\n"
                "    Variables can be used as function arguments, like test.dll void print(str \"%%d\", $var1)\n"
                "    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'\n"
                "Usage in interactive mode is the same as non-interactive except for the focused DLL,\n"
                "then you don't need to specify <dll_path>\n"
                "Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, voidptr, void\n"
                "    Equivalent to int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t,\n"
                "    float, double, null-terminated string, pointer (always hex), and void\n"
                "You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type.\n"
            );
            return 1;
        }

        // Reconstruct the command line excluding the program name for the parser
        char* p = GetCommandLineA();
        
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
