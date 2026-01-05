#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include "common.hpp"

extern "C" extern uint64_t call_dynamic_function(void* func_ptr, uint64_t* args, int arg_count, uint32_t float_mask);

void handle_quit_command() {
    for (int i = 0; i < g_registry_count; i++) FreeLibrary(g_registry[i].handle);
    exit(0);
}

uint64_t handle_alloc_command(char* p) {
    p = skip_ws(p + 6);
    char* expanded = expand_vars(p);
    size_t size = strtoull(expanded, NULL, 0);
    void* ptr = malloc(size);
    print("0x%llX\n", (uint64_t)ptr);
    free(expanded);
    return (uint64_t)ptr; // Return the address
}

void handle_free_command(char* p) {
    p = skip_ws(p + 5);
    char* expanded = expand_vars(p);
    void* ptr = (void*)strtoull(expanded, NULL, 16);
    free(ptr);
    print("Freed memory at 0x%llX\n", (uint64_t)ptr);
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
        case TypeKind::TYPE_I8:  *(int8_t*)addr  = (int8_t)value; break;
        case TypeKind::TYPE_I16: *(int16_t*)addr = (int16_t)value; break;
        case TypeKind::TYPE_I32: *(int32_t*)addr = (int32_t)value; break;
        case TypeKind::TYPE_I64: *(int64_t*)addr = (int64_t)value; break;
        case TypeKind::TYPE_U8:  *(uint8_t*)addr  = (uint8_t)value; break;
        case TypeKind::TYPE_U16: *(uint16_t*)addr = (uint16_t)value; break;
        case TypeKind::TYPE_U32: *(uint32_t*)addr = (uint32_t)value; break;
        case TypeKind::TYPE_U64: *(uint64_t*)addr = (uint64_t)value; break;
        case TypeKind::TYPE_F32: { float f; memcpy(&f,&value,sizeof(float)); *(float*)addr = f; } break;
        case TypeKind::TYPE_F64: { double d; memcpy(&d,&value,sizeof(double)); *(double*)addr = d; } break;
        case TypeKind::TYPE_STR: 
            if (value != 0) strcpy((char*)addr, (char*)value);
            break;
        case TypeKind::TYPE_WSTR:
            if (value != 0) wcscpy((wchar_t*)addr, (wchar_t*)value);
            // Ideally we should free((void*)value) here because parse_argument_value allocated it
            // free((void*)value); 
            break;
        case TypeKind::TYPE_PTR: *(uint64_t*)addr = value; break;
        default: printf("unsupported\n"); break;
    }
    
    char res_buf[256];
    format_result(value, type, res_buf, sizeof(res_buf));
    print("Value at 0x%llX (%s): %s\n", addr, type_str, res_buf);
    
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
        print("Set %zu bytes at 0x%llX to 0x%02x\n", count, addr, value);
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
        case TypeKind::TYPE_I8:  value = (uint64_t)*(int8_t*)addr; break;
        case TypeKind::TYPE_I16: value = (uint64_t)*(int16_t*)addr; break;
        case TypeKind::TYPE_I32: value = (uint64_t)*(int32_t*)addr; break;
        case TypeKind::TYPE_I64: value = (uint64_t)*(int64_t*)addr; break;
        case TypeKind::TYPE_U8:  value = (uint64_t)*(uint8_t*)addr; break;
        case TypeKind::TYPE_U16: value = (uint64_t)*(uint16_t*)addr; break;
        case TypeKind::TYPE_U32: value = (uint64_t)*(uint32_t*)addr; break;
        case TypeKind::TYPE_U64: value = (uint64_t)*(uint64_t*)addr; break;
        case TypeKind::TYPE_F32: { float f = *(float*)addr; memcpy(&value,&f,sizeof(float)); } break;
        case TypeKind::TYPE_F64: { double d = *(double*)addr; memcpy(&value,&d,sizeof(double)); } break;
        case TypeKind::TYPE_STR: value = addr; break;
        case TypeKind::TYPE_WSTR: value = addr; break;
        case TypeKind::TYPE_PTR: value = *(uint64_t*)addr; break;
        default: printf("unsupported\n"); break;
    }
    
    char res_buf[256];
    format_result(value, type, res_buf, sizeof(res_buf));
    print("Value at 0x%llX (%s): %s\n", addr, type_str, res_buf);
    
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

HMODULE find_registered_dll(const char* path, int* out_idx) {
    for (int i = 0; i < g_registry_count; i++) {
        if (_stricmp(g_registry[i].path, path) == 0) {
            if (out_idx) *out_idx = i;
            return g_registry[i].handle;
        }
    }
    return NULL;
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
                    print("[Auto-Registered: %s]\n", dll_path);
                } else {
                    printf("Error: Registry full, cannot pin DLL.\n");
                    if (!g_interactive)
                        exit(1);
                }
            } else {
                printf("Error: Could not load %s\n", dll_path);
                if (!g_interactive)
                    exit(1);
            }
        }

        if (h) {
            result_addr = (uint64_t)GetProcAddress(h, name_str);
            if (result_addr) {
                print("0x%llX\n", result_addr);
            } else {
                printf("Error: Symbol '%s' not found in %s\n", name_str, dll_path);
                if (!g_interactive)
                    exit(1);
            }
        }
    }

    free(dll_path); 
    free(name_str); 
    free(expanded);
    return result_addr;
}

int calc_padding(int offset, int alignment) {
    int remainder = offset % alignment;
    return remainder == 0 ? 0 : alignment - remainder;
}

int round_up(int size, int alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
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

size_t handle_struct_command(char* p) {
    p = skip_ws(p + 7); // skip "/struct"
    if (*p != '{') { printf("Expected '{'\n"); return 0; }
    p++; // skip '{'

    Struct* current = &g_known_structs[g_known_struct_count++];
    current->member_count = 0;

    // Locate the closing brace to bound our parsing
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); return 0; }

    // We'll work on a copy of the content inside { ... }
    int body_len = brace_end - p;
    char* struct_body = (char*)malloc(body_len + 1);
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
                        print("$%s = 0x%X\n", var_name, offset);
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

    print("Offset | Size | Align | Type    | Name\n");
    print("------ | ---- | ----- | ------- | --------------------\n");
    
    int last_end = 0;
    for (int i = 0; i < current->member_count; i++) {
        Member *m = &current->members[i];
        
        // Print padding if any
        if (m->offset > last_end) {
            int padding = m->offset - last_end;
            print("0x%04X | (%d bytes padding)\n", last_end, padding);
        }
        
        char type_str[64];
        snprintf(type_str, sizeof(type_str), "%s", type_to_string(m->type));
        
        if (m->array_size > 0) {
            char array_str[80];
            snprintf(array_str, sizeof(array_str), "%s[%d]", type_str, m->array_size);
            print("0x%04X | %-4d | %-5d | %-7s | %s\n", 
                   m->offset, m->size, m->alignment, array_str, m->name);
        } else {
            print("0x%04X | %-4d | %-5d | %-7s | %s\n", 
                   m->offset, m->size, m->alignment, type_str, m->name);
        }
        
        last_end = m->offset + m->size;
    }
    
    // Print trailing padding
    if (current->total_size > last_end) {
        int padding = current->total_size - last_end;
        print("0x%04X | (%d bytes trailing padding)\n", last_end, padding);
    }
    
    print("Total size: %d bytes (0x%X)\n", current->total_size, current->total_size);
    print("Alignment: %d bytes\n", current->alignment);

    free(struct_body);

    return current->total_size;
}

void handle_loaddll_command(char* p) {
    p = skip_ws(p + 8);
    char* expanded = expand_vars(p);

    if (find_registered_dll(expanded, &g_focus_idx)) {
        print("DLL '%s' is already loaded (Focus set).\n", expanded);
    } else if (g_registry_count < 32) {
        HMODULE h = LoadLibraryA(expanded);
        if (h) {
            strncpy(g_registry[g_registry_count].path, expanded, 255);
            g_registry[g_registry_count].handle = h;
            g_focus_idx = g_registry_count;
            g_registry_count++;
            print("Loaded and registered: %s (Focus set)\n", expanded);
        } else {
            printf("Error: Could not load '%s' (Error: %lu)\n", expanded, GetLastError());
            if (!g_interactive)
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
        print("Unloaded: %s\n", expanded);
    } else {
        printf("DLL '%s' not found in registry.\n", expanded);
    }
    
    free(expanded);
}

void handle_dlls_command() {
    print("Registered DLLs (%d/%d)\n", g_registry_count, 32);
    
    if (g_registry_count == 0) {
        print("No DLLs loaded.\n");
        return;
    }

    print("%-3s %-10s %s\n", "ID", "Handle", "Path");

    for (int i = 0; i < g_registry_count; i++) {
        // Use an asterisk or arrow to indicate the focused DLL
        char focus_char = (i == g_focus_idx) ? '>' : ' ';
        
        print("%c%02d [0x%p] %s\n", 
               focus_char, 
               i, 
               g_registry[i].handle, 
               g_registry[i].path);
    }
}

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
                if (!g_interactive)
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
        if (!g_interactive)
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
            if (!g_interactive)
                exit(1);
            return 0;
        }
    }
    
    // 6. EXECUTION
    if (!func_ptr) {
        printf("Error: Could not resolve function '%s'.\n", spec.func_name);
        // Clean up arguments before returning
        for (int j = 0; j < spec.arg_count; j++) {
            if (spec.args[j].type == TypeKind::TYPE_STR && spec.args[j].type == TypeKind::TYPE_WSTR) free((void*)spec.args[j].value);
        }
        free(expanded);
        if (!g_interactive)
            exit(1);
        return 0;
    }

    uint64_t args[16] = {0};
    uint32_t float_mask = 0;
    for (int j = 0; j < spec.arg_count; j++) {
        args[j] = spec.args[j].value;
        if (spec.args[j].type == TypeKind::TYPE_F32 || spec.args[j].type == TypeKind::TYPE_F64) {
            float_mask |= (1 << j);
        }
    }
    if (spec.return_type == TypeKind::TYPE_F32 || spec.return_type == TypeKind::TYPE_F64) {
        float_mask |= 0x80000000; // Set the 31st bit
    }

    //print("DEBUG: Jumping to %p with %d args\n", func_ptr, spec.arg_count);
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
            if (spec.args[j].type == TypeKind::TYPE_STR) free((void*)spec.args[j].value);
        }
        free(expanded);
        
        // In script mode, a crash is fatal. In interactive, we let the user decide.
        if (!g_interactive) exit(1);
        return 0;
    }
    
    // 7. POST-CALL: Formatting & Assertions
    if (spec.print_result && spec.return_type != TypeKind::TYPE_VOID) {
        char buf[256];
        format_result(result, spec.return_type, buf, sizeof(buf));
        print("Result: %s\n", buf);
    }
    
    g_assert_failed = false;
    if (spec.assert_type != AssertType::ASSERT_NONE) {
        switch (spec.assert_type) {
            case AssertType::ASSERT_ZERO: if (result != 0) g_assert_failed = true; break;
            case AssertType::ASSERT_NOT_ZERO: if (result == 0) g_assert_failed = true; break;
            case AssertType::ASSERT_NEGATIVE: if ((int64_t)result >= 0) g_assert_failed = true; break;
            case AssertType::ASSERT_NON_NEGATIVE: if ((int64_t)result < 0) g_assert_failed = true; break;
            default: break;
        }
        if (g_assert_failed && !g_in_a_loop) {
            char buf[256];
            format_result(result, spec.return_type, buf, sizeof(buf));
            printf("Assertion failed for result: %s\n", buf);
        }
    }

    // Cleanup
    for (int j = 0; j < spec.arg_count; j++) {
        if (spec.args[j].type == TypeKind::TYPE_STR && spec.args[j].type == TypeKind::TYPE_WSTR) free((void*)spec.args[j].value);
    }
    
    free(expanded);
    return (spec.return_type != TypeKind::TYPE_VOID) ? result : 0;
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
    if (*p != '{') { printf("Expected '{' after count\n"); if (!g_interactive) exit(1); return; }
    p++; // skip '{'

    // Locate the closing brace
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); if (!g_interactive) exit(1); return; }

    g_in_a_loop = true;

    // Copy the content inside { ... }
    int body_len = brace_end - p;
    char* loop_body = (char*)malloc(body_len + 1);
    memcpy(loop_body, p, body_len);
    loop_body[body_len] = '\0';

    Variable* i_var = find_var("i");
    if (!i_var) {
        printf("Error! Could not find the loop variable\n");
        if (!g_interactive)
            exit(2);
        return;
    }

    // 2. Execute the loop
    for (long i = 0; i < count; i++) {
        i_var->value = i;
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
    g_in_a_loop = false;
}

void handle_repeat_until_command(char* input_line) {
    uint64_t process_command(char* input_line);
    char* p = skip_ws(input_line + 13); // skip "/repeat-until"
    if (!*p) return;

    if (*p != '{') { printf("Expected '{' after /repeat-until\n"); if (!g_interactive) exit(1); return; }
    p++; // skip '{'

    // Locate the closing brace
    char* brace_end = strchr(p, '}');
    if (!brace_end) { printf("Expected '}'\n"); if (!g_interactive) exit(1); return; }

    g_in_a_loop = true;

    // Copy the content inside { ... }
    int body_len = brace_end - p;
    char* loop_body = (char*)malloc(body_len + 1);
    memcpy(loop_body, p, body_len);
    loop_body[body_len] = '\0';

    Variable* i_var = find_var("i");
    if (!i_var) {
        printf("Error! Could not find the loop variable\n");
        if (!g_interactive)
            exit(2);
        return;
    }

    // 2. Execute the loop until assert fails
    size_t i = 0;
    while (true) {
        i_var->value = i++;
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
                
                if (g_assert_failed) {
                    g_assert_failed = false;
                    g_in_a_loop = false;
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
    g_in_a_loop = false;
}
