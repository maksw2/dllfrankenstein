#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "common.hpp"

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
    char* copy = (char*)malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

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
            if (!g_interactive)
                exit(1);
            return 0;
        }
        val = *(uint64_t*)val;
    }

    return val;
}

char* expand_vars(const char* input) {
    size_t buf_size = strlen(input) + 1;
    char* result = (char*)malloc(buf_size);
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

        char* new_result = (char*)malloc(new_size);
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

TypeKind parse_type(const char* str) {
    if (strcmp(str,"i8")==0 || strcmp(str,"int8_t")==0) return TypeKind::TYPE_I8;
    else if (strcmp(str,"i16")==0 || strcmp(str,"int16_t")==0) return TypeKind::TYPE_I16;
    else if (strcmp(str,"i32")==0 || strcmp(str,"int32_t")==0) return TypeKind::TYPE_I32;
    else if (strcmp(str,"i64")==0 || strcmp(str,"int64_t")==0) return TypeKind::TYPE_I64;
    else if (strcmp(str,"u8")==0 || strcmp(str,"uint8_t")==0) return TypeKind::TYPE_U8;
    else if (strcmp(str,"u16")==0 || strcmp(str,"uint16_t")==0) return TypeKind::TYPE_U16;
    else if (strcmp(str,"u32")==0 || strcmp(str,"uint32_t")==0) return TypeKind::TYPE_U32;
    else if (strcmp(str,"u64")==0 || strcmp(str,"uint64_t")==0) return TypeKind::TYPE_U64;
    else if (strcmp(str,"f32")==0 || strcmp(str,"float")==0) return TypeKind::TYPE_F32;
    else if (strcmp(str,"f64")==0 || strcmp(str,"double")==0) return TypeKind::TYPE_F64;
    else if (strcmp(str,"str")==0) return TypeKind::TYPE_STR;
    else if (strcmp(str,"wstr")==0) return TypeKind::TYPE_WSTR;
    else if (strcmp(str,"voidptr")==0) return TypeKind::TYPE_PTR;
    else if (strcmp(str,"void")==0) return TypeKind::TYPE_VOID;
    return TypeKind::TYPE_VOID;
}

uint64_t parse_argument_value(TypeKind type, const char* str) {
    switch(type){
        case TypeKind::TYPE_I8: case TypeKind::TYPE_I16: case TypeKind::TYPE_I32: case TypeKind::TYPE_I64:
            return (uint64_t)strtoll(str,NULL,0);
        case TypeKind::TYPE_U8: case TypeKind::TYPE_U16: case TypeKind::TYPE_U32: case TypeKind::TYPE_U64:
            return (uint64_t)strtoull(str,NULL,0);
        case TypeKind::TYPE_F32: {
            float f_val = (float)atof(str);
            uint64_t u64_bits = 0; // Important: Clear high bits
            memcpy(&u64_bits, &f_val, sizeof(float)); // Copy only 4 bytes
            return u64_bits;
        }
        case TypeKind::TYPE_F64: {
            double d_val = atof(str); // Parse directly to double
            uint64_t u64_bits;
            memcpy(&u64_bits, &d_val, sizeof(double)); // Get 64-bit pattern of the double
            return u64_bits;
        }
        case TypeKind::TYPE_STR: 
        case TypeKind::TYPE_WSTR: {
            int len = (int)strlen(str);
            char* src = (char*)str;
            int start = 0, end = len;

            // Remove quotes if present
            if(len >= 2 && str[0] == '"' && str[len-1] == '"') {
                start = 1;
                end = len - 1;
            }

            // Allocate temporary narrow buffer
            char* dest = (char*)malloc(end - start + 1);
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
            if (type == TypeKind::TYPE_STR) {
                return (uint64_t)dest;
            }

            // If it is a WSTR, convert to UTF-16
            // Calculate required length
            int wlen = MultiByteToWideChar(CP_UTF8, 0, dest, -1, NULL, 0);
            wchar_t* wdest = (wchar_t*)malloc(wlen * sizeof(wchar_t));
            if (!wdest) {
                free(dest);
                return 0;
            }
            
            // Perform conversion
            MultiByteToWideChar(CP_UTF8, 0, dest, -1, wdest, wlen);
            
            free(dest); // Free the narrow temp buffer
            return (uint64_t)wdest;
        }
        case TypeKind::TYPE_PTR: return strtoull(str,NULL,16);
        default: return 0;
    }
}

void format_result(uint64_t result, TypeKind type, char* buf, size_t size) {
    switch(type){
        case TypeKind::TYPE_I8: snprintf(buf,size,"%d",(int8_t)result); break;
        case TypeKind::TYPE_I16: snprintf(buf,size,"%d",(int16_t)result); break;
        case TypeKind::TYPE_I32: snprintf(buf,size,"%d",(int32_t)result); break;
        case TypeKind::TYPE_I64: snprintf(buf,size,"%lld",(int64_t)result); break;
        case TypeKind::TYPE_U8: snprintf(buf,size,"%u",(uint8_t)result); break;
        case TypeKind::TYPE_U16: snprintf(buf,size,"%u",(uint16_t)result); break;
        case TypeKind::TYPE_U32: snprintf(buf,size,"%u",(uint32_t)result); break;
        case TypeKind::TYPE_U64: snprintf(buf,size,"%llu",(uint64_t)result); break;
        case TypeKind::TYPE_F32: { float f; memcpy(&f,&result,sizeof(float)); snprintf(buf,size,"%f",f); break; }
        case TypeKind::TYPE_F64: { double d; memcpy(&d,&result,sizeof(double)); snprintf(buf,size,"%f",d); break; }
        case TypeKind::TYPE_STR: snprintf(buf,size,"%s",(char*)result); break;
        case TypeKind::TYPE_WSTR: {
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
        case TypeKind::TYPE_PTR: snprintf(buf,size,"0x%llX",result); break;
        case TypeKind::TYPE_VOID: snprintf(buf,size,"(void)"); break;
    }
}

char* read_token(char** s) {
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
    token = (char*)malloc(len+1);
    strncpy(token,start,len);
    token[len]=0;

    if(*p==',' || *p==')') p++;
    *s = skip_ws(p);
    return token;
}

int parse_header(char** s, char* ret_type, char* func_name) {
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

void parse_arguments(char** s, CallSpec* spec) {
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
            spec->assert_type = AssertType::ASSERT_ZERO;
            p += 13;
        } else if (strncmp(p, "--assert=nonzero", 16) == 0) {
            spec->assert_type = AssertType::ASSERT_NOT_ZERO;
            p += 16;
        } else if (strncmp(p, "--assert=negative", 16) == 0) {
            spec->assert_type = AssertType::ASSERT_NEGATIVE;
            p += 16;
        } else if (strncmp(p, "--assert=nonnegative", 20) == 0) {
            spec->assert_type = AssertType::ASSERT_NON_NEGATIVE;
            p += 20;
        } else if (strncmp(p, "--assert", 8) == 0) {
            spec->assert_type = AssertType::ASSERT_ZERO;
            p += 8;
        } else {
            // Move to next potential flag
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
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
        case TypeKind::TYPE_I8: return "i8";
        case TypeKind::TYPE_U8: return "u8";
        case TypeKind::TYPE_I16: return "i16";
        case TypeKind::TYPE_U16: return "u16";
        case TypeKind::TYPE_I32: return "i32";
        case TypeKind::TYPE_U32: return "u32";
        case TypeKind::TYPE_I64: return "i64";
        case TypeKind::TYPE_U64: return "u64";
        case TypeKind::TYPE_F32: return "f32";
        case TypeKind::TYPE_F64: return "f64";
        case TypeKind::TYPE_PTR: return "voidptr";
        default: return "unknown";
    }
}

void get_type_info(TypeKind type, int *size, int *align) {
    switch (type) {
        case TypeKind::TYPE_I8:
        case TypeKind::TYPE_U8:
            *size = 1; *align = 1; break;
        case TypeKind::TYPE_I16:
        case TypeKind::TYPE_U16:
            *size = 2; *align = 2; break;
        case TypeKind::TYPE_I32:
        case TypeKind::TYPE_U32:
        case TypeKind::TYPE_F32:
            *size = 4; *align = 4; break;
        case TypeKind::TYPE_I64:
        case TypeKind::TYPE_U64:
        case TypeKind::TYPE_F64:
        case TypeKind::TYPE_PTR:
            *size = 8; *align = 8; break;
        default:
            *size = 0; *align = 1;
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
    char* res = (char*)malloc(len + 1);
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
                print("$%s = 0x%llX\n", var_name, result);
            }
            return result;
        }
    }

    // Command dispatch
    if (*p == '/') {
        if (match_cmd(p, "/quit", false))             { handle_quit_command(); }
        else if (match_cmd(p, "/enable-normal-variables-pretty-please", false)) { g_normal_vars = true; return 0; }
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
        
        if (t != TypeKind::TYPE_VOID) { 
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
                    return handle_function_call(input_line);
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

    return handle_function_call(input_line);
}
