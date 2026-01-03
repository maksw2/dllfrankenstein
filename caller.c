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
    TYPE_STR, TYPE_WSTR,
    TYPE_PTR, TYPE_VOID
} TypeKind;

typedef enum {
    ASSERT_NONE,
    ASSERT_ZERO,
    ASSERT_NOT_ZERO,
    ASSERT_NEGATIVE,
    ASSERT_NON_NEGATIVE
} AssertType;

typedef struct {
    TypeKind type;
    uint64_t value;
} Argument;

typedef struct {
    char dll_path[256];
    char func_name[128];
    TypeKind return_type;
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

typedef struct Member {
    char name[64];
    TypeKind type;
    int array_size;
    char struct_name[64];
    int offset;
    int size;
    int alignment;
} Member;

typedef struct Struct {
    Member members[128];
    int member_count;
    int total_size;
    int alignment;
} Struct;

Struct known_structs[256];
int known_struct_count = 0;

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
            if (!interactive)
                    exit(1);
            return false;
        }
        existing->value = value;
        existing->is_set = true;
        return true;
    }
    
    if (g_var_count >= 128) {
        printf("Error: Maximum number of variables reached\n");
        if (!interactive)
                exit(1);
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

char* strndup(const char* s, size_t n) {
    char* copy = malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

// Helper to get a value from either a literal or a variable
uint64_t get_operand_value(char* p, char** endptr) {
    p = skip_ws(p);
    
    bool deref = false;
    bool addr_of = false;

    // Support multiple or combined prefixes (e.g., *&$var)
    while (*p == '*' || *p == '&') {
        if (*p == '*') deref = !deref; 
        if (*p == '&') addr_of = !addr_of;
        p++;
    }

    uint64_t val = 0;
    if (*p == '$') {
        char* name_start = p + 1;
        char* name_end = name_start;
        while (*name_end && (isalnum((unsigned char)*name_end) || *name_end == '_'))
            name_end++;
        
        size_t len = name_end - name_start;
        char* var_name = strndup(name_start, len);
        Variable* var = find_var(var_name);
        free(var_name);
        
        *endptr = name_end;
        if (!var) return 0;

        // If & was used, we want the address of the value field in the struct
        val = addr_of ? (uint64_t)&(var->value) : var->value;
    } else {
        val = strtoull(p, endptr, 0);
    }

    // If * was used, treat the current value as an address and peek
    if (deref) {
        if (val == 0) {
            fprintf(stderr, "Error: Null pointer dereference\n");
            return 0;
        }
        val = *(uint64_t*)val;
    }

    return val;
}

char* expand_vars(const char* input) {
    size_t buf_size = strlen(input) + 1;
    char* result = malloc(buf_size);
    if (!result) return NULL;
    strcpy(result, input);

    bool changed;
    do {
        changed = false;
        char* dollar = strchr(result, '$');
        if (!dollar) break;

        // Determine where the replacement starts (include prefix if exists)
        char* token_start = dollar;
        if (dollar > result && (*(dollar - 1) == '&' || *(dollar - 1) == '*')) {
            token_start = dollar - 1;
            // Handle double prefixes like **$var or *&$var
            while (token_start > result && (*(token_start - 1) == '&' || *(token_start - 1) == '*')) {
                token_start--;
            }
        }

        char* endptr;
        // get_operand_value now starts at the prefix (if any)
        uint64_t final_val = get_operand_value(token_start, &endptr);
        char* expr_end = endptr;

        // --- FULL ARITHMETIC LOGIC RESTORED ---
        char* p = skip_ws(expr_end);
        while (*p) {
            char op = *p;
            char op2 = *(p + 1);
            
            if ((op == '<' && op2 == '<') || (op == '>' && op2 == '>')) {
                p += 2;
                uint64_t operand = get_operand_value(p, &endptr);
                if (endptr == p) break;
                if (op == '<') final_val <<= operand;
                else final_val >>= operand;
                p = endptr;
                expr_end = p;
            }
            else if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%') {
                // Peek ahead: don't treat '*' as multiplication if it's a deref prefix for the next var
                char* next_p = skip_ws(p + 1);
                if (op == '*' && (*next_p == '*' || *next_p == '&' || *next_p == '$')) {
                    // This is likely a multiplication, but get_operand_value handles the next part
                }

                p++;
                uint64_t operand = get_operand_value(p, &endptr);
                if (endptr == p) break;
                switch(op) {
                    case '+': final_val += operand; break;
                    case '-': final_val -= operand; break;
                    case '*': final_val *= operand; break;
                    case '/': if (operand != 0) final_val /= operand; break;
                    case '%': if (operand != 0) final_val %= operand; break;
                }
                p = endptr;
                expr_end = p;
            } else {
                break;
            }
            p = skip_ws(p);
        }

        // Generate the replacement string
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "0x%llX", final_val);

        size_t prefix_part_len = token_start - result;
        size_t val_len = strlen(val_buf);
        size_t suffix_len = strlen(expr_end);
        size_t new_size = prefix_part_len + val_len + suffix_len + 1;

        char* new_result = malloc(new_size);
        if (!new_result) { free(result); return NULL; }

        // Construct the new string: [text before operator][hex value][text after expression]
        memcpy(new_result, result, prefix_part_len);
        memcpy(new_result + prefix_part_len, val_buf, val_len);
        memcpy(new_result + prefix_part_len + val_len, expr_end, suffix_len + 1);

        free(result);
        result = new_result;
        changed = true;

    } while (changed);

    return result;
}

// ------------------------ Helpers ------------------------
TypeKind parse_type(const char* str) {
    if (strcmp(str,"i8")==0 || strcmp(str,"int8_t")==0) return TYPE_I8;
    else if (strcmp(str,"i16")==0 || strcmp(str,"int16_t")==0) return TYPE_I16;
    else if (strcmp(str,"i32")==0 || strcmp(str,"int32_t")==0) return TYPE_I32;
    else if (strcmp(str,"i64")==0 || strcmp(str,"int64_t")==0) return TYPE_I64;
    else if (strcmp(str,"u8")==0 || strcmp(str,"uint8_t")==0) return TYPE_U8;
    else if (strcmp(str,"u16")==0 || strcmp(str,"uint16_t")==0) return TYPE_U16;
    else if (strcmp(str,"u32")==0 || strcmp(str,"uint32_t")==0) return TYPE_U32;
    else if (strcmp(str,"u64")==0 || strcmp(str,"uint64_t")==0) return TYPE_U64;
    else if (strcmp(str,"f32")==0 || strcmp(str,"float")==0) return TYPE_F32;
    else if (strcmp(str,"f64")==0 || strcmp(str,"double")==0) return TYPE_F64;
    else if (strcmp(str,"str")==0) return TYPE_STR;
    else if (strcmp(str,"wstr")==0) return TYPE_WSTR;
    else if (strcmp(str,"voidptr")==0) return TYPE_PTR;
    else if (strcmp(str,"void")==0) return TYPE_VOID;
    return TYPE_VOID;
}

uint64_t parse_argument_value(TypeKind type, const char* str) {
    switch(type){
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
            return (uint64_t)strtoll(str,NULL,0);
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
            return (uint64_t)strtoull(str,NULL,0);
        case TYPE_F32: {
            float f_val = (float)atof(str);
            uint64_t u64_bits = 0; // Important: Clear high bits
            memcpy(&u64_bits, &f_val, sizeof(float)); // Copy only 4 bytes
            return u64_bits;
        }
        case TYPE_F64: {
            double d_val = atof(str); // Parse directly to double
            uint64_t u64_bits;
            memcpy(&u64_bits, &d_val, sizeof(double)); // Get 64-bit pattern of the double
            return u64_bits;
        }
        case TYPE_STR: 
        case TYPE_WSTR: {
            int len = (int)strlen(str);
            char* src = (char*)str;
            int start = 0, end = len;

            // Remove quotes if present
            if(len >= 2 && str[0] == '"' && str[len-1] == '"') {
                start = 1;
                end = len - 1;
            }

            // Allocate temporary narrow buffer
            char* dest = malloc(end - start + 1);
            if (!dest) return 0;

            int j = 0;
            for (int i = start; i < end; i++) {
                if (src[i] == '\\' && i + 1 < end) {
                    switch (src[i + 1]) {
                        case 'n': dest[j++] = '\n'; i++; break;
                        case 'r': dest[j++] = '\r'; i++; break;
                        case 't': dest[j++] = '\t'; i++; break;
                        case '\\': dest[j++] = '\\'; i++; break;
                        case '\"': dest[j++] = '\"'; i++; break;
                        default:  dest[j++] = src[i]; break; 
                    }
                } else {
                    dest[j++] = src[i];
                }
            }
            dest[j] = '\0';

            // If it is just a normal string, return it
            if (type == TYPE_STR) {
                return (uint64_t)dest;
            }

            // If it is a WSTR, convert to UTF-16
            // Calculate required length
            int wlen = MultiByteToWideChar(CP_UTF8, 0, dest, -1, NULL, 0);
            wchar_t* wdest = malloc(wlen * sizeof(wchar_t));
            if (!wdest) {
                free(dest);
                return 0;
            }
            
            // Perform conversion
            MultiByteToWideChar(CP_UTF8, 0, dest, -1, wdest, wlen);
            
            free(dest); // Free the narrow temp buffer
            return (uint64_t)wdest;
        }
        case TYPE_PTR: return strtoull(str,NULL,16);
        default: return 0;
    }
}

void format_result(uint64_t result, TypeKind type, char* buf, size_t size){
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
        case TYPE_WSTR: {
            if (result == 0) {
                snprintf(buf, size, "(null)");
            } else {
                // Convert Wide to Narrow for display
                WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)result, -1, buf, (int)size, NULL, NULL);
                // Ensure null termination in case buffer was too small
                buf[size - 1] = '\0';
            }
            break;
        }
        case TYPE_PTR: snprintf(buf,size,"0x%llX",result); break;
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

        TypeKind t = parse_type(type_token);
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
int g_focus_idx = -1;

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

void get_type_info(TypeKind type, int *size, int *align) {
    switch (type) {
        case TYPE_I8:
        case TYPE_U8:
            *size = 1; *align = 1; break;
        case TYPE_I16:
        case TYPE_U16:
            *size = 2; *align = 2; break;
        case TYPE_I32:
        case TYPE_U32:
        case TYPE_F32:
            *size = 4; *align = 4; break;
        case TYPE_I64:
        case TYPE_U64:
        case TYPE_F64:
        case TYPE_PTR:
            *size = 8; *align = 8; break;
        default:
            *size = 0; *align = 1;
    }
}

int calc_padding(int offset, int alignment) {
    int remainder = offset % alignment;
    return remainder == 0 ? 0 : alignment - remainder;
}

int round_up(int size, int alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}

void trim(char *str) {
    char *start = str;
    while (*start && isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

const char* type_to_string(TypeKind type) {
    switch (type) {
        case TYPE_I8: return "i8";
        case TYPE_U8: return "u8";
        case TYPE_I16: return "i16";
        case TYPE_U16: return "u16";
        case TYPE_I32: return "i32";
        case TYPE_U32: return "u32";
        case TYPE_I64: return "i64";
        case TYPE_U64: return "u64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_PTR: return "voidptr";
        default: return "unknown";
    }
}

void calculate_struct_layout(Struct *s) {
    int offset = 0;
    int max_align = 1;
    
    for (int i = 0; i < s->member_count; i++) {
        Member *m = &s->members[i];
        int size, align;
        
        get_type_info(m->type, &size, &align);
        
        if (m->array_size > 0) {
            size *= m->array_size;
        }
        
        int padding = calc_padding(offset, align);
        offset += padding;
        
        m->offset = offset;
        m->size = size;
        m->alignment = align;
        
        offset += size;
        
        if (align > max_align) {
            max_align = align;
        }
    }
    
    s->alignment = max_align;
    s->total_size = round_up(offset, max_align);
}

void print_struct(Struct *s) {
    printf("Offset | Size | Align | Type    | Name\n");
    printf("------ | ---- | ----- | ------- | --------------------\n");
    
    int last_end = 0;
    for (int i = 0; i < s->member_count; i++) {
        Member *m = &s->members[i];
        
        // Print padding if any
        if (m->offset > last_end) {
            int padding = m->offset - last_end;
            printf("0x%04X | (%d bytes padding)\n", last_end, padding);
        }
        
        char type_str[64];
        snprintf(type_str, sizeof(type_str), "%s", type_to_string(m->type));
        
        if (m->array_size > 0) {
            char array_str[80];
            snprintf(array_str, sizeof(array_str), "%s[%d]", type_str, m->array_size);
            printf("0x%04X | %-4d | %-5d | %-7s | %s\n", 
                   m->offset, m->size, m->alignment, array_str, m->name);
        } else {
            printf("0x%04X | %-4d | %-5d | %-7s | %s\n", 
                   m->offset, m->size, m->alignment, type_str, m->name);
        }
        
        last_end = m->offset + m->size;
    }
    
    // Print trailing padding
    if (s->total_size > last_end) {
        int padding = s->total_size - last_end;
        printf("0x%04X | (%d bytes trailing padding)\n", last_end, padding);
    }
    
    printf("Total size: %d bytes (0x%X)\n", s->total_size, s->total_size);
    printf("Alignment: %d bytes\n", s->alignment);
}

void handle_quit_command() {
    for (int i = 0; i < g_registry_count; i++) FreeLibrary(g_registry[i].handle);
    exit(0);
}

uint64_t handle_alloc_command(char* p) {
    p = skip_ws(p + 6);
    char* expanded = expand_vars(p);
    size_t size = strtoull(expanded, NULL, 0);
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
    TypeKind type = parse_type(type_str);
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
        case TYPE_WSTR:
            if (value != 0) wcscpy((wchar_t*)addr, (wchar_t*)value);
            // Ideally we should free((void*)value) here because parse_argument_value allocated it
            // free((void*)value); 
            break;
        case TYPE_PTR: *(uint64_t*)addr = value; break;
        default: printf("unsupported\n"); break;
    }
    
    char res_buf[256];
    format_result(value, type, res_buf, sizeof(res_buf));
    printf("Value at 0x%llX (%s): %s\n", addr, type_str, res_buf);
    
    free(addr_str); free(type_str); free(val_str);
    free(expanded);
}

void handle_memset_command(char* p) {
    p = skip_ws(p + 7); // Skip "/memset"
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    
    char* addr_str = read_token(&work_p);
    char* val_str = read_token(&work_p);
    char* count_str = read_token(&work_p);

    if (!addr_str || !val_str || !count_str) {
        printf("Error: /memset requires <addr> <value> <count>\n");
        if (addr_str) free(addr_str);
        if (val_str) free(val_str);
        if (count_str) free(count_str);
        free(expanded);
        return;
    }

    // Use get_operand_value or strtoull to resolve the tokens
    // This ensures that even if expand_vars didn't catch it, we try to resolve $vars
    char* endptr;
    uint64_t addr = get_operand_value(addr_str, &endptr);
    // If the token wasn't a variable, get_operand_value defaults to strtoull(base 16)
    if (addr_str[0] != '$') addr = strtoull(addr_str, NULL, 16);

    uint8_t value = (uint8_t)get_operand_value(val_str, &endptr);
    size_t count = (size_t)get_operand_value(count_str, &endptr);

    if (addr != 0) {
        memset((void*)addr, value, count);
        printf("Set %zu bytes at 0x%llX to 0x%02x\n", count, addr, value);
    } else {
        printf("Error: Invalid address for memset\n");
    }

    free(addr_str); 
    free(val_str); 
    free(count_str);
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
    TypeKind type = parse_type(type_str);

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
        case TYPE_WSTR: value = addr; break;
        case TYPE_PTR: value = *(uint64_t*)addr; break;
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
    
    fprintf(stderr, "Dump of 0x%llX (%d bytes):\n", (uint64_t)data, count);
    for (int i = 0; i < count; i += 16) {
        fprintf(stderr, "  %llX: ", (uint64_t)(data + i));
        
        for (int j = 0; j < 16; j++) {
            if (i + j < count)
                fprintf(stderr, "%02X ", data[i + j]);
            else
                fprintf(stderr, "   ");
        }
        
        fprintf(stderr, " |");
        for (int j = 0; j < 16; j++) {
            if (i + j < count) {
                unsigned char c = data[i + j];
                fprintf(stderr, "%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        fprintf(stderr, "|\n");
    }

    free(addr_str); 
    if(count_str) free(count_str);
    free(expanded);
}

uint64_t handle_address_command(char* p) {
    p = skip_ws(p + 8); // Skip "/address"
    char* expanded = expand_vars(p);
    char* work_p = expanded;
    char* dll_path = read_token(&work_p);
    char* name_str = read_token(&work_p);
    
    uint64_t result_addr = 0;
    if (dll_path && name_str) {
        // 1. Check if it's already registered
        HMODULE h = find_registered_dll(dll_path, NULL);
        
        if (!h) { 
            // 2. Not found? Load it properly
            h = LoadLibraryA(dll_path); 
            if (h) {
                // 3. Add to registry so it's "pinned" in memory
                if (g_registry_count < 32) {
                    strncpy(g_registry[g_registry_count].path, dll_path, 255);
                    g_registry[g_registry_count].handle = h;
                    g_focus_idx = g_registry_count; // Set focus to this DLL
                    g_registry_count++;
                    printf("[Auto-Registered: %s]\n", dll_path);
                } else {
                    printf("Error: Registry full, cannot pin DLL.\n");
                    if (!interactive)
                        exit(1);
                }
            } else {
                printf("Error: Could not load %s\n", dll_path);
                if (!interactive)
                    exit(1);
            }
        }

        if (h) {
            result_addr = (uint64_t)GetProcAddress(h, name_str);
            if (result_addr) {
                printf("0x%llX\n", result_addr);
            } else {
                printf("Error: Symbol '%s' not found in %s\n", name_str, dll_path);
                if (!interactive)
                    exit(1);
            }
        }
    }

    free(dll_path); 
    free(name_str); 
    free(expanded);
    return result_addr;
}

size_t handle_struct_command(char* p) {
    p = skip_ws(p + 7); // skip "/struct"
    if (*p != '{') { printf("Expected '{'\n"); return 0; }
    p++; // skip '{'

    Struct* current = &known_structs[known_struct_count++];
    current->member_count = 0;

    // Locate the closing brace to bound our parsing
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); return 0; }

    // We'll work on a copy of the content inside { ... }
    int body_len = brace_end - p;
    char* struct_body = malloc(body_len + 1);
    memcpy(struct_body, p, body_len);
    struct_body[body_len] = '\0';

    char* cursor = struct_body;
    int offset = 0;
    int max_align = 1;

    // Manual replacement for strtok loop
    while (cursor && *cursor != '\0') {
        char* next_comma = strchr(cursor, ',');
        if (next_comma) *next_comma = '\0';

        char* member_str = cursor; 
        trim(member_str);

        if (strlen(member_str) > 0) {
            char* var_name = NULL;
            if (*member_str == '$') {
                char* eq = strchr(member_str, '=');
                if (eq) {
                    *eq = '\0';
                    var_name = strdup(member_str + 1);
                    member_str = eq + 1;
                    trim(member_str);
                    trim(var_name);
                }
            }

            char type_str[64], field_name[64];
            int array_size = 0;

            char* bracket = strchr(member_str, '[');
            if (bracket) {
                char* close = strchr(bracket, ']');
                if (close) {
                    *close = '\0';
                    array_size = atoi(bracket + 1);
                    *bracket = '\0';
                }
            }

            char* space = strchr(member_str, ' ');
            if (space) {
                *space = '\0';
                strcpy(type_str, member_str);
                strcpy(field_name, space + 1);
                trim(type_str);
                trim(field_name);

                Member* m = &current->members[current->member_count++];
                strcpy(m->name, field_name);
                m->type = parse_type(type_str);
                m->array_size = array_size;

                int size, align;
                get_type_info(m->type, &size, &align);
                if (array_size) size *= array_size;

                int padding = calc_padding(offset, align);
                offset += padding;

                m->offset = offset;
                m->size = size;
                m->alignment = align;

                if (var_name) {
                    if (set_var(var_name, offset)) {
                        printf("$%s = 0x%X\n", var_name, offset);
                    }
                    free(var_name);
                }

                offset += size;
                if (align > max_align) max_align = align;
            } else {
                if (strlen(member_str) > 0) printf("Bad member: %s\n", member_str);
            }
        }

        // Move cursor to the start of the next member
        cursor = next_comma ? (next_comma + 1) : NULL;
    }

    current->total_size = round_up(offset, max_align);
    current->alignment = max_align;

    print_struct(current);
    free(struct_body);

    return current->total_size;
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
            if (!interactive)
                exit(1);
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

void handle_dlls_command() {
    printf("Registered DLLs (%d/%d)\n", g_registry_count, 32);
    
    if (g_registry_count == 0) {
        printf("No DLLs loaded.\n");
        return;
    }

    printf("%-3s %-10s %s\n", "ID", "Handle", "Path");

    for (int i = 0; i < g_registry_count; i++) {
        // Use an asterisk or arrow to indicate the focused DLL
        char focus_char = (i == g_focus_idx) ? '>' : ' ';
        
        printf("%c%02d [0x%p] %s\n", 
               focus_char, 
               i, 
               g_registry[i].handle, 
               g_registry[i].path);
    }
}

bool assert_failed = false;

uint64_t handle_function_call(char* input_line) {
    char* p = input_line;
    // 1. Expand variables (e.g., $print becomes 0x7FFCA03D4CDC)
    char* expanded = expand_vars(p);
    p = expanded;

    CallSpec spec = {0};
    char* flags_ptr = NULL;
    int in_quotes = 0;

    // 2. Extract flags (trailing -- assertions or print settings)
    for (char* s = p; *s; s++) {
        if (*s == '"') in_quotes = !in_quotes;
        if (!in_quotes && s[0] == '-' && s[1] == '-') { 
            flags_ptr = s; 
            break; 
        }
    }
    if (flags_ptr) { 
        parse_flags(flags_ptr, &spec); 
        *flags_ptr = '\0'; 
    }

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

    // 3. Determine DLL Context
    if (strstr(first_token, ".dll") != NULL) {
        target_dll = find_registered_dll(first_token, NULL);
        if (!target_dll) {
            target_dll = LoadLibraryA(first_token);
            if (!target_dll) {
                printf("Error: Failed to load %s\n", first_token);
                free(expanded);
                if (!interactive)
                    exit(1);
                return 0;
            }
        }
        p = temp_p;
    } else {
        // Use focused DLL if no explicit DLL is provided
        if (g_focus_idx != -1) {
            target_dll = g_registry[g_focus_idx].handle;
        }
    }

    // 4. Parse Return Type and Function Name (or Hex Address)
    char ret_type_str[32];
    if (!parse_header(&p, ret_type_str, spec.func_name)) {
        printf("Error: Signature parse failed\n");
        free(expanded);
        if (!interactive)
            exit(1);
        return 0;
    }
    spec.return_type = parse_type(ret_type_str);
    parse_arguments(&p, &spec);

    void* func_ptr = NULL;

    // 5. RESOLVE FUNCTION POINTER
    // Case A: Direct Address (Hexadecimal)
    if (spec.func_name[0] == '0' && (spec.func_name[1] == 'x' || spec.func_name[1] == 'X')) {
        func_ptr = (void*)_strtoui64(spec.func_name, NULL, 16);
    } 
    // Case B: Ordinal Lookup (starts with #)
    else if (spec.func_name[0] == '#') {
        if (target_dll) {
            char* end = NULL;
            long ordinal = strtol(spec.func_name + 1, &end, 10);
            if (end != spec.func_name + 1 && ordinal > 0 && ordinal <= 0xFFFF) {
                func_ptr = (void*)GetProcAddress(target_dll, MAKEINTRESOURCEA((WORD)ordinal));
            }
        }
    } 
    // Case C: Standard Export Lookup
    else {
        if (target_dll) {
            func_ptr = (void*)GetProcAddress(target_dll, spec.func_name);
        } else {
            printf("Error: No DLL context to find function '%s'. Use a DLL name or focus one.\n", spec.func_name);
            free(expanded);
            if (!interactive)
                exit(1);
            return 0;
        }
    }
    
    // 6. EXECUTION
    if (!func_ptr) {
        printf("Error: Could not resolve function '%s'.\n", spec.func_name);
        // Clean up arguments before returning
        for (int j = 0; j < spec.arg_count; j++) {
            if (spec.args[j].type == TYPE_STR && spec.args[j].type == TYPE_WSTR) free((void*)spec.args[j].value);
        }
        free(expanded);
        if (!interactive)
            exit(1);
        return 0;
    }

    uint64_t args[16] = {0};
    uint32_t float_mask = 0;
    for (int j = 0; j < spec.arg_count; j++) {
        args[j] = spec.args[j].value;
        if (spec.args[j].type == TYPE_F32 || spec.args[j].type == TYPE_F64) {
            float_mask |= (1 << j);
        }
    }
    if (spec.return_type == TYPE_F32 || spec.return_type == TYPE_F64) {
        float_mask |= 0x80000000; // Set the 31st bit
    }

    //printf("DEBUG: Jumping to %p with %d args\n", func_ptr, spec.arg_count);
    uint64_t result = 0;
    bool crash_detected = false;
    unsigned long exception_code_ = 0;

    __try {
        result = call_dynamic_function(func_ptr, args, spec.arg_count, float_mask);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exception_code_ = GetExceptionCode();
        crash_detected = true;
    }

    if (crash_detected) {
        printf("\n[!!!] CRASH DETECTED DURING CALL [!!!]\n");
        printf("Exception Code: 0x%08lX\n", exception_code_);
        
        switch(exception_code_) {
            case EXCEPTION_ACCESS_VIOLATION:    printf("Reason: Access Violation\n"); break;
            case EXCEPTION_STACK_OVERFLOW:      printf("Reason: Stack Overflow\n"); break;
            case EXCEPTION_ILLEGAL_INSTRUCTION: printf("Reason: Illegal Instruction\n"); break;
            case EXCEPTION_PRIV_INSTRUCTION:    printf("Reason: Privileged Instruction\n"); break;
        }

        // Emergency Cleanup
        for (int j = 0; j < spec.arg_count; j++) {
            if (spec.args[j].type == TYPE_STR) free((void*)spec.args[j].value);
        }
        free(expanded);
        
        // In script mode, a crash is fatal. In interactive, we let the user decide.
        if (!interactive) exit(1);
        return 0;
    }
    
    // 7. POST-CALL: Formatting & Assertions
    if (spec.print_result && spec.return_type != TYPE_VOID) {
        char buf[256];
        format_result(result, spec.return_type, buf, sizeof(buf));
        printf("Result: %s\n", buf);
    }
    
    assert_failed = false;
    if (spec.assert_type != ASSERT_NONE) {
        switch (spec.assert_type) {
            case ASSERT_ZERO: if (result != 0) assert_failed = true; break;
            case ASSERT_NOT_ZERO: if (result == 0) assert_failed = true; break;
            case ASSERT_NEGATIVE: if ((int64_t)result >= 0) assert_failed = true; break;
            case ASSERT_NON_NEGATIVE: if ((int64_t)result < 0) assert_failed = true; break;
            default: break;
        }
        if (assert_failed) {
            char buf[256];
            format_result(result, spec.return_type, buf, sizeof(buf));
            printf("Assertion failed for result: %s\n", buf);
        }
    }

    // Cleanup
    for (int j = 0; j < spec.arg_count; j++) {
        if (spec.args[j].type == TYPE_STR && spec.args[j].type == TYPE_WSTR) free((void*)spec.args[j].value);
    }
    
    free(expanded);
    return (spec.return_type != TYPE_VOID) ? result : 0;
}

void handle_for_command(char* input_line) {
    uint64_t process_command(char* input_line);
    char* p = skip_ws(input_line + 4); // skip "/for"
    if (!*p) return;

    // 1. Parse loop count
    char* endptr = NULL;
    long count = strtol(p, &endptr, 0);
    if (count <= 0) return;

    p = skip_ws(endptr);
    if (*p != '{') { printf("Expected '{' after count\n"); return; }
    p++; // skip '{'

    // Locate the closing brace
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); return; }

    // Copy the content inside { ... }
    int body_len = brace_end - p;
    char* loop_body = malloc(body_len + 1);
    memcpy(loop_body, p, body_len);
    loop_body[body_len] = '\0';

    // 2. Execute the loop
    for (long i = 0; i < count; i++) {
        // We need a fresh copy each iteration since we modify it
        char* body_copy = strdup(loop_body);
        char* cursor = body_copy;
        
        // Parse comma-separated commands (respecting parentheses)
        while (*cursor != '\0') {
            // Skip leading whitespace
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
                cursor++;
            }
            if (*cursor == '\0') break;

            // Find the start of this command
            char* cmd_start = cursor;
            
            // Find next comma that's not inside parentheses
            int paren_depth = 0;
            while (*cursor != '\0') {
                if (*cursor == '(') {
                    paren_depth++;
                } else if (*cursor == ')') {
                    paren_depth--;
                } else if (*cursor == ',' && paren_depth == 0) {
                    break;
                }
                cursor++;
            }

            // Extract and process this command
            char saved = *cursor;
            *cursor = '\0';
            
            char* cmd = cmd_start;
            trim(cmd);
            
            if (strlen(cmd) > 0) {
                process_command(cmd);
            }

            // Move past the comma if we found one
            if (saved == ',') {
                *cursor = saved;
                cursor++;
            }
        }
        
        free(body_copy);
    }

    free(loop_body);
}

void handle_repeat_until_command(char* input_line) {
    uint64_t process_command(char* input_line);
    char* p = skip_ws(input_line + 13); // skip "/repeat-until"
    if (!*p) return;

    if (*p != '{') { printf("Expected '{' after /repeat-until\n"); return; }
    p++; // skip '{'

    // Locate the closing brace
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); return; }

    // Copy the content inside { ... }
    int body_len = brace_end - p;
    char* loop_body = malloc(body_len + 1);
    memcpy(loop_body, p, body_len);
    loop_body[body_len] = '\0';

    // 2. Execute the loop until assert fails
    while (true) {
        // We need a fresh copy each iteration since we modify it
        char* body_copy = strdup(loop_body);
        char* cursor = body_copy;
        
        // Parse comma-separated commands (respecting parentheses)
        while (*cursor != '\0') {
            // Skip leading whitespace
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
                cursor++;
            }
            if (*cursor == '\0') break;

            // Find the start of this command
            char* cmd_start = cursor;
            
            // Find next comma that's not inside parentheses
            int paren_depth = 0;
            while (*cursor != '\0') {
                if (*cursor == '(') {
                    paren_depth++;
                } else if (*cursor == ')') {
                    paren_depth--;
                } else if (*cursor == ',' && paren_depth == 0) {
                    break;
                }
                cursor++;
            }

            // Extract and process this command
            char saved = *cursor;
            *cursor = '\0';
            
            char* cmd = cmd_start;
            trim(cmd);
            
            if (strlen(cmd) > 0) {
                process_command(cmd);
                
                if (assert_failed) {
                    assert_failed = false;
                    free(body_copy);
                    free(loop_body);
                    return;
                }
            }

            // Move past the comma if we found one
            if (saved == ',') {
                *cursor = saved;
                cursor++;
            }
        }
        
        free(body_copy);
    }
}

int match_cmd(const char* input, const char* cmd, int allow_args) {
    size_t len = strlen(cmd);
    if (strncmp(input, cmd, len) != 0) return 0;
    char next = input[len];
    if (allow_args) return next == ' ' || next == '\0';
    else return next == '\0';
}

char* read_quoted_string(char** cursor) {
    char* p = *cursor;
    p = skip_ws(p);
    if (*p != '"') return NULL;
    
    char* start = p;
    p++; // skip initial quote
    while (*p && (*p != '"' || *(p-1) == '\\')) { // Handle escaped quotes \"
        p++;
    }
    if (*p == '"') p++; // skip closing quote
    
    size_t len = p - start;
    char* res = malloc(len + 1);
    memcpy(res, start, len);
    res[len] = '\0';
    
    *cursor = p; // advance the main pointer past the string
    return res;
}

uint64_t process_command(char* input_line) {
    char* p = skip_ws(input_line);
    if (!*p) return 0;

    bool in_quote = false;
    for (char* c = p; *c; c++) {
        if (*c == '"') {
            in_quote = !in_quote;
        }
        if (*c == ';' && !in_quote) {
            *c = '\0';
            break;
        }
    }
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
        if (match_cmd(p, "/quit", false))             { handle_quit_command(); }
        else if (match_cmd(p, "/enable-normal-variables-pretty-please", false)) { normal_vars = true; return 0; }
        else if (match_cmd(p, "/alloc", true))        { return handle_alloc_command(p); }
        else if (match_cmd(p, "/free", true))         { handle_free_command(p); return 0; }
        else if (match_cmd(p, "/set", true))          { handle_set_command(p); return 0; }
        else if (match_cmd(p, "/memset", true))       { handle_memset_command(p); return 0; }
        else if (match_cmd(p, "/get", true))          { return handle_get_command(p); }
        else if (match_cmd(p, "/hex", true))          { handle_hex_command(p); return 0; }
        else if (match_cmd(p, "/address", true))      { return handle_address_command(p); }
        else if (match_cmd(p, "/struct", true))       { return handle_struct_command(p); }
        else if (match_cmd(p, "/loaddll", true))      { handle_loaddll_command(p); return 0; }
        else if (match_cmd(p, "/freedll", true))      { handle_freedll_command(p); return 0; }
        else if (match_cmd(p, "/dlls", false))        { handle_dlls_command(); return 0; }
        else if (match_cmd(p, "/for", true))          { handle_for_command(p); return 0; }
        else if (match_cmd(p, "/repeat-until", true)) { handle_repeat_until_command(p); return 0; }
        else { printf("Unknown command: %s\n", p); return 0; }
    }

    char* temp_p = p;
    char* first_token = read_token(&temp_p);
    if (first_token) {
        TypeKind t = parse_type(first_token);
        
        if (t != TYPE_VOID) { 
            char* val_ptr = skip_ws(temp_p);
            
            // 1. STRINGS: If it starts with ", it's a string literal.
            if (*val_ptr == '"') {
                char* val_str = read_quoted_string(&temp_p); 
                uint64_t result = parse_argument_value(t, val_str);
                free(val_str); free(first_token);
                return result;
            }

            // 2. FUNCTION CALLS (Standard or Variable-based)
            // Peek at the next token to see if it's followed by '('
            char* peek_p = temp_p;
            char* val_str = read_token(&peek_p);
            if (val_str) {
                char* next_char = skip_ws(peek_p);
                // If it's "Type $var(" or "Type func("
                if (*next_char == '(' || strchr(val_str, '(')) {
                    free(val_str);
                    free(first_token);
                    goto trigger_function; 
                }
                
                // 3. LITERAL VALUES: (e.g., i32 42 or voidptr $some_var)
                // Note: If there's no '(', it's just a value.
                uint64_t result = parse_argument_value(t, val_str);
                free(val_str);
                free(first_token);
                return result;
            }
        }
        free(first_token);
    }

trigger_function:
    //printf("Calling function: %s\n", input_line);
    return handle_function_call(input_line);
}

int main(int argc, char** argv) {
    bool help = false;
    const char* script_file = NULL;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            help = true;
            interactive = false;
            script_file = NULL;
            break;
        }
        else if (strcmp(argv[i], "--interactive") == 0) {
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
        printf("wilczurski's cool shit - script\n");
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
            printf("> %s\n", line);
            process_command(line);
            line = strtok(NULL, "\n");
            if (assert_failed)
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
                "    /for     <count>    {<cmd>}...      Repeat {commands} <count> times\n"
                "    /repeat-until       {<cmd>}...      Repeat {commands} until assert\n"
                "    /quit                               Exit the program\n"
                "Variables by default are Write-Once Read-Many, no shadowing, no scopes.\n"
                "    --normal-variables-pretty-please allows reassignment. Not recommended.\n"
                "    $<name> = <type> <value>    Set variable value (e.g. $val = i32 10)\n"
                "    $<name> = <command>         Capture command/function output into variable\n"
                "    &$<name>                    Address-of: Get the memory pointer to a variable's storage\n"
                "    *$<name>                    Dereference: Read 64-bit value from the address stored in $<name>\n"
                "    Variables can be used as function arguments, like test.dll void print(str \"%%d\", i32 $var1)\n"
                "    Variables can store arbitrary data, values like '$a = i32 69' or pointers like '$p = voidptr 0x12345678'\n"
                "Types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, str, wstr, voidptr, void\n"
                "    Or their \"proper\" version: int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double,\n"
                "    str, wstr, voidptr are equivelant to C's \"narrow\" null-terminated string (char*), wide string (wchar_t*), pointer (void*, always hex)\n"
                "    In the case of 'str' interpretation is entirely up to the callee (ACP, UTF-8, ASCII, or raw bytes). No validation or conversion is performed.\n"
                "You can pass hex and decimal values; strtoll or strtoull will evaluate them depending on type (except pointers).\n"
            );
            return 1;
        }
        printf("wilczurski's cool shit - one-shot\n");

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
