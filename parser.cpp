#include "parser.hpp"
#include <windows.h>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <bit>
#include <algorithm>
#include <unordered_map>

// --- Globals ---
struct RegisteredDLL {
    char path[256];
    HMODULE handle;
};

static std::unordered_map<std::string, char*> g_str_cache;
static std::unordered_map<std::string, wchar_t*> g_wstr_cache;

static RegisteredDLL g_registry[32];
static int g_registry_count = 0;
static int g_focus_idx = -1;
static bool g_assert_failed = false;

// Extern the assembly bridge
extern "C" uint64_t call_dynamic_function(void* func_ptr, uint64_t* args, int arg_count, uint32_t float_mask);

// --- Context Helpers ---

static Token peek(ParseContext* ctx, int offset = 0) {
    if (ctx->pos + offset >= ctx->tokens.size()) return {TokenType::TOK_EOF, "", 0, 0};
    return ctx->tokens[ctx->pos + offset];
}

static Token advance(ParseContext* ctx) {
    if (ctx->pos >= ctx->tokens.size()) throw IncompleteInput();
    return ctx->tokens[ctx->pos++];
}

static bool match(ParseContext* ctx, enum TokenType type) {
    if (peek(ctx).type == type) {
        advance(ctx);
        return true;
    }
    return false;
}

static Token consume(ParseContext* ctx, enum TokenType type, const char* err) {
    Token t = peek(ctx);
    if (t.type == type) return advance(ctx);
    if (t.type == TokenType::TOK_EOF) throw IncompleteInput();
    throw SyntaxError(std::string(err) + " (Got: " + t.text + ")", t.line);
}

// --- String Cache Helpers ---

static char* get_cached_string(const std::string& s) {
    if (g_str_cache.find(s) == g_str_cache.end()) {
        g_str_cache[s] = _strdup(s.c_str());
    }
    return g_str_cache[s];
}

static wchar_t* get_cached_wstring(const std::string& s) {
    if (g_wstr_cache.find(s) == g_wstr_cache.end()) {
        int req_chars = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
        wchar_t* wbuf = (wchar_t*)malloc(req_chars * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, wbuf, req_chars);
        g_wstr_cache[s] = wbuf;
    }
    return g_wstr_cache[s];
}

// --- Type System ---

static TypeKind token_to_type(const std::string& s) {
    if (s == "i8"  || s == "int8_t")   return TypeKind::TYPE_I8;
    if (s == "i16" || s == "int16_t")  return TypeKind::TYPE_I16;
    if (s == "i32" || s == "int32_t")  return TypeKind::TYPE_I32;
    if (s == "i64" || s == "int64_t")  return TypeKind::TYPE_I64;
    if (s == "u8"  || s == "uint8_t")  return TypeKind::TYPE_U8;
    if (s == "u16" || s == "uint16_t") return TypeKind::TYPE_U16;
    if (s == "u32" || s == "uint32_t") return TypeKind::TYPE_U32;
    if (s == "u64" || s == "uint64_t") return TypeKind::TYPE_U64;
    if (s == "f32" || s == "float")    return TypeKind::TYPE_F32;
    if (s == "f64" || s == "double")   return TypeKind::TYPE_F64;
    if (s == "str")                    return TypeKind::TYPE_STR;
    if (s == "wstr")                   return TypeKind::TYPE_WSTR;
    if (s == "voidptr")                return TypeKind::TYPE_PTR;
    if (s == "void")                   return TypeKind::TYPE_VOID;
    return TypeKind::TYPE_VOID;
}

static int get_type_size(TypeKind t) {
    switch(t) {
        case TypeKind::TYPE_I8: case TypeKind::TYPE_U8: return 1;
        case TypeKind::TYPE_I16: case TypeKind::TYPE_U16: return 2;
        case TypeKind::TYPE_I32: case TypeKind::TYPE_U32: case TypeKind::TYPE_F32: return 4;
        case TypeKind::TYPE_I64: case TypeKind::TYPE_U64: case TypeKind::TYPE_F64: case TypeKind::TYPE_PTR: case TypeKind::TYPE_STR: case TypeKind::TYPE_WSTR: return 8;
        default: return 0;
    }
}

static const char* type_to_string(TypeKind t) {
    switch(t) {
        case TypeKind::TYPE_I8: return "i8";
        case TypeKind::TYPE_I16: return "i16";
        case TypeKind::TYPE_I32: return "i32";
        case TypeKind::TYPE_I64: return "i64";
        case TypeKind::TYPE_U8: return "u8";
        case TypeKind::TYPE_U16: return "u16";
        case TypeKind::TYPE_U32: return "u32";
        case TypeKind::TYPE_U64: return "u64";
        case TypeKind::TYPE_F32: return "f32";
        case TypeKind::TYPE_F64: return "f64";
        case TypeKind::TYPE_STR: return "str";
        case TypeKind::TYPE_WSTR: return "wstr";
        case TypeKind::TYPE_PTR: return "voidptr";
        default: return "unknown";
    }
}

static void format_result(uint64_t val, TypeKind type, char* buf, size_t size) {
    switch (type) {
        case TypeKind::TYPE_I8: snprintf(buf, size, "%d", (int8_t)val); break;
        case TypeKind::TYPE_U8: snprintf(buf, size, "%u", (uint8_t)val); break;
        case TypeKind::TYPE_I16: snprintf(buf, size, "%d", (int16_t)val); break;
        case TypeKind::TYPE_U16: snprintf(buf, size, "%u", (uint16_t)val); break;
        case TypeKind::TYPE_I32: snprintf(buf, size, "%d", (int32_t)val); break;
        case TypeKind::TYPE_U32: snprintf(buf, size, "%u", (uint32_t)val); break;
        case TypeKind::TYPE_I64: snprintf(buf, size, "%lld", (int64_t)val); break;
        case TypeKind::TYPE_U64: snprintf(buf, size, "%llu", (uint64_t)val); break;
        case TypeKind::TYPE_F32: { float f; memcpy(&f, &val, 4); snprintf(buf, size, "%.4f", f); } break;
        case TypeKind::TYPE_F64: { double d; memcpy(&d, &val, 8); snprintf(buf, size, "%.6f", d); } break;
        case TypeKind::TYPE_STR: snprintf(buf, size, "\"%s\"", (val ? (char*)val : "NULL")); break;
        case TypeKind::TYPE_WSTR: {
            wchar_t* wptr = (wchar_t*)val;
            if (!wptr) {
                snprintf(buf, size, "wstr NULL");
            } else {
                char mb_buf[1024]; 
                int n = WideCharToMultiByte(CP_UTF8, 0, wptr, -1, mb_buf, sizeof(mb_buf)-1, NULL, NULL);
                if (n == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) mb_buf[sizeof(mb_buf)-1] = 0;
                snprintf(buf, size, "wstr \"%s\"", mb_buf);
            }
            break;
        }
        case TypeKind::TYPE_PTR: snprintf(buf, size, "0x%llX", val); break;
        default: snprintf(buf, size, "?"); break;
    }
}

// --- Expression Parser (Forward Decls) ---

struct Layout {
    uint64_t size;
    int align;
};

static Value parse_expression(ParseContext* ctx);
static uint64_t handle_address(ParseContext* ctx);
static Layout handle_struct(ParseContext* ctx, std::string member_name = "root");

// this fucking bitch
static Value parse_factor(ParseContext* ctx) {
    Token t = peek(ctx);

    // 1. Handle Unary Minus (-5, -0.5)
    if (match(ctx, TokenType::TOK_MINUS)) {
        Value val = parse_factor(ctx);
        if (val.type == TypeKind::TYPE_F32 || val.type == TypeKind::TYPE_F64) {
            float f = *(float*)&val.value;
            f = -f;
            val.value = *(uint32_t*)&f;
        } else {
            val.value = -val.value;
        }
        return val;
    }

    // 2. Handle Address-Of (&$var)
    if (match(ctx, TokenType::TOK_AMP)) {
        Token var_t = consume(ctx, TokenType::TOK_VARIABLE, "Expected variable after &");
        uint64_t addr = var_get_addr(var_t.text);
        return { TypeKind::TYPE_PTR, addr }; // Returns pointer to the variable's value slot
    }

    // 3. Dereference (*$var) -> Reads 64-bit value from address
    if (match(ctx, TokenType::TOK_STAR)) {
        Value addr = parse_factor(ctx); // Recurse to allow *($ptr + 8)
        if (addr.value == 0) throw SyntaxError("Null pointer dereference", t.line);
        // Read 8 bytes from the target address
        return { TypeKind::TYPE_U64, *(uint64_t*)addr.value };
    }

    // 4. Directives as Values
    if (match(ctx, TokenType::TOK_SLASH)) {
        Token cmd = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected command");
        
        if (cmd.text == "address") {
            // /address <dll> <name>
            return { TypeKind::TYPE_PTR, handle_address(ctx) };
        }
        else if (cmd.text == "struct") {
             return { TypeKind::TYPE_U64, handle_struct(ctx).size };
        }
        
        throw SyntaxError("Unexpected directive in expression: " + cmd.text, cmd.line);
    }

    // 5. Type Casts (i32 0x..., wstr "...")
    if (t.type == TokenType::TOK_IDENTIFIER) {
        TypeKind type = token_to_type(t.text);
        if (type != TypeKind::TYPE_VOID) { 
            advance(ctx);

            // If we are casting a STRING literal to WSTR, convert using CACHE
            if (type == TypeKind::TYPE_WSTR && peek(ctx).type == TokenType::TOK_STRING) {
                std::string full_str = "";
                while (peek(ctx).type == TokenType::TOK_STRING) {
                    full_str += advance(ctx).text;
                }
                return { TypeKind::TYPE_STR, (uint64_t)get_cached_wstring(full_str) };
            }

            return parse_factor(ctx); 
        }
    }

    if (match(ctx, TokenType::TOK_INTEGER)) return { TypeKind::TYPE_I64, t.int_val };
    
    if (match(ctx, TokenType::TOK_FLOAT)) {
        float f = std::stof(t.text);
        return { TypeKind::TYPE_F32, std::bit_cast<uint32_t>(f) };
    }

    if (match(ctx, TokenType::TOK_VARIABLE)) {
        Value val; bool is_f;
        if (!var_get(t.text, &val)) throw SyntaxError("Undefined: " + t.text, t.line);
        return val;
    }

    // 6. Handle Strings (and concatenation)
    if (match(ctx, TokenType::TOK_STRING)) {
        std::string full_str = t.text;
        // Check for adjacent strings ("A" "B")
        while (peek(ctx).type == TokenType::TOK_STRING) {
            full_str += advance(ctx).text;
        }
        // Use Cache!
        return { TypeKind::TYPE_STR, (uint64_t)get_cached_string(full_str) };
    }

    if (match(ctx, TokenType::TOK_LPAREN)) {
        Value v = parse_expression(ctx);
        consume(ctx, TokenType::TOK_RPAREN, "Expected ')'");
        return v;
    }

    throw SyntaxError("Expected value, got " + t.text, t.line);
}

// Helper to cast "bits" to float, do op, and cast back
static Value math_op(Value left, Value right, char op) {
    bool res_float = left.type == TypeKind::TYPE_F32 || left.type == TypeKind::TYPE_F64
     || right.type == TypeKind::TYPE_F32 || right.type == TypeKind::TYPE_F64;

    if (res_float) {
        float l_val = left.type == TypeKind::TYPE_F32 || left.type == TypeKind::TYPE_F64 ? *(float*)&left.value : (float)(int64_t)left.value;
        float r_val = right.type == TypeKind::TYPE_F32 || right.type == TypeKind::TYPE_F64 ? *(float*)&right.value : (float)(int64_t)right.value;
        float res = 0.0f;

        switch(op) {
            case '+': res = l_val + r_val; break;
            case '-': res = l_val - r_val; break;
            case '*': res = l_val * r_val; break;
            case '/': res = (r_val != 0) ? (l_val / r_val) : 0; break;
        }
        return { TypeKind::TYPE_F32, *(uint32_t*)&res };
    } else {
        uint64_t res = 0;
        switch(op) {
            case '+': res = left.value + right.value; break;
            case '-': res = left.value - right.value; break;
            case '*': res = left.value * right.value; break;
            case '/': res = (right.value != 0) ? (left.value / right.value) : 0; break;
        }
        return { TypeKind::TYPE_I64, res };
    }
}

static Value parse_term(ParseContext* ctx) {
    Value left = parse_factor(ctx);
    while (peek(ctx).type == TokenType::TOK_STAR || peek(ctx).type == TokenType::TOK_SLASH) {
        Token op = peek(ctx);
        // If we see a '/', check if it's on a NEW LINE.
        // If so, it's a Directive (/loaddll), not Division.
        // We stop parsing the expression here so parse_statement can handle the directive.
        if (op.type == TokenType::TOK_SLASH) {
            // ctx->pos is currently at the operator.
            // Check the line number of the PREVIOUS token (the Factor we just consumed).
            int last_line = ctx->tokens[ctx->pos - 1].line;
            if (op.line > last_line) {
                break; // Stop! Let parse_statement handle the slash.
            }
        }
        advance(ctx); // eat op
        Value right = parse_factor(ctx);
        left = math_op(left, right, (op.type == TokenType::TOK_STAR) ? '*' : '/');
    }
    return left;
}

static Value parse_expression(ParseContext* ctx) {
    Value left = parse_term(ctx);
    while (peek(ctx).type == TokenType::TOK_PLUS || peek(ctx).type == TokenType::TOK_MINUS) {
        enum TokenType op = advance(ctx).type;
        Value right = parse_term(ctx);
        left = math_op(left, right, (op == TokenType::TOK_PLUS) ? '+' : '-');
    }
    return left;
}

// --- Handler Implementations ---

static void handle_set(ParseContext* ctx) {
    // /set <addr> <type> <val>
    uint64_t addr = parse_expression(ctx).value;
    Token type_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected type");
    TypeKind type = token_to_type(type_tok.text);
    
    uint64_t val = 0;

    // Special handling for string literals to perform conversion if needed
    if (type == TypeKind::TYPE_STR && peek(ctx).type == TokenType::TOK_STRING) {
        Token s = advance(ctx);
        val = (uint64_t)strdup(s.text.c_str()); 
    }
    else if (type == TypeKind::TYPE_WSTR && peek(ctx).type == TokenType::TOK_STRING) {
        // Auto-convert literal string to wstr
        std::string s = advance(ctx).text;
        int req = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
        wchar_t* wbuf = (wchar_t*)malloc(req * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, wbuf, req);
        val = (uint64_t)wbuf;
    }
    else {
        val = parse_expression(ctx).value;
    }

    switch(type){
        case TypeKind::TYPE_I8:  *(int8_t*)addr  = (int8_t)val; break;
        case TypeKind::TYPE_I16: *(int16_t*)addr = (int16_t)val; break;
        case TypeKind::TYPE_I32: *(int32_t*)addr = (int32_t)val; break;
        case TypeKind::TYPE_I64: *(int64_t*)addr = (int64_t)val; break;
        case TypeKind::TYPE_U8:  *(uint8_t*)addr  = (uint8_t)val; break;
        case TypeKind::TYPE_U16: *(uint16_t*)addr = (uint16_t)val; break;
        case TypeKind::TYPE_U32: *(uint32_t*)addr = (uint32_t)val; break;
        case TypeKind::TYPE_U64: *(uint64_t*)addr = (uint64_t)val; break;
        case TypeKind::TYPE_F32: { float f; memcpy(&f,&val,4); *(float*)addr = f; } break;
        case TypeKind::TYPE_F64: { double d; memcpy(&d,&val,8); *(double*)addr = d; } break;
        case TypeKind::TYPE_STR: if (val) strcpy((char*)addr, (char*)val); break;
        case TypeKind::TYPE_WSTR: if (val) wcscpy((wchar_t*)addr, (wchar_t*)val); break;
        case TypeKind::TYPE_PTR: *(uint64_t*)addr = val; break;
        default: printf("Unsupported set type\n"); break;
    }
    
    char buf[128];
    format_result(val, type, buf, sizeof(buf));
    printf("Value at 0x%llX (%s): %s\n", addr, type_tok.text.c_str(), buf);
}

static void handle_get(ParseContext* ctx) {
    // /get <addr> <type>
    uint64_t addr = parse_expression(ctx).value;
    Token type_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected type");
    TypeKind type = token_to_type(type_tok.text);

    uint64_t val = 0;
    switch(type){
        case TypeKind::TYPE_I8:  val = (uint64_t)*(int8_t*)addr; break;
        case TypeKind::TYPE_I16: val = (uint64_t)*(int16_t*)addr; break;
        case TypeKind::TYPE_I32: val = (uint64_t)*(int32_t*)addr; break;
        case TypeKind::TYPE_I64: val = (uint64_t)*(int64_t*)addr; break;
        case TypeKind::TYPE_U8:  val = (uint64_t)*(uint8_t*)addr; break;
        case TypeKind::TYPE_U16: val = (uint64_t)*(uint16_t*)addr; break;
        case TypeKind::TYPE_U32: val = (uint64_t)*(uint32_t*)addr; break;
        case TypeKind::TYPE_U64: val = (uint64_t)*(uint64_t*)addr; break;
        case TypeKind::TYPE_F32: { float f = *(float*)addr; memcpy(&val,&f,4); } break;
        case TypeKind::TYPE_F64: { double d = *(double*)addr; memcpy(&val,&d,8); } break;
        case TypeKind::TYPE_STR: val = addr; break;
        case TypeKind::TYPE_WSTR: val = addr; break;
        case TypeKind::TYPE_PTR: val = *(uint64_t*)addr; break;
        default: break;
    }

    char buf[128];
    format_result(val, type, buf, sizeof(buf));
    printf("Read 0x%llX (%s): %s\n", addr, type_tok.text.c_str(), buf);
}

static void handle_hex(ParseContext* ctx) {
    uint64_t addr = parse_expression(ctx).value;
    uint64_t count = 64;
    // Check if next token is start of a number or expression
    if (peek(ctx).type == TokenType::TOK_INTEGER || peek(ctx).type == TokenType::TOK_VARIABLE) {
        count = parse_expression(ctx).value;
    }
    if (count == 0) count = 16;
    
    unsigned char* data = (unsigned char*)addr;
    printf("Dump 0x%llX (%lld bytes):\n", addr, count);
    
    for (uint64_t i = 0; i < count; i += 16) {
        printf("  %04llX: ", i);
        for (int j = 0; j < 16; j++) {
            if (i+j < count) printf("%02X ", data[i+j]);
            else printf("   ");
        }
        printf(" |");
        for (int j = 0; j < 16; j++) {
            if (i+j < count) {
                unsigned char c = data[i+j];
                printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
        }
        printf("|\n");
    }
}

static uint64_t handle_address(ParseContext* ctx) {
    // /address <dll> <name>
    Token dll_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected DLL");
    std::string dll_name = dll_tok.text;
    Token func_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected function name");
    std::string func_name = func_tok.text;

    void* func_ptr = nullptr;

    HMODULE h = LoadLibraryA(dll_name.c_str());
    if (h) {
        if (func_name[0] == '#') {
            char* end_ptr;
            unsigned long ord = strtoul(func_name.c_str() + 1, &end_ptr, 10);
            
            // Validation:
            // 1. Check if we actually parsed digits (end_ptr moved)
            // 2. Check if it fits in a 16-bit WORD (Max 65535)
            if (end_ptr == func_name.c_str() + 1 || ord > 0xFFFF) {
                throw SyntaxError("Invalid ordinal: " + std::string(func_name), func_tok.line);
            }

            func_ptr = (void*)GetProcAddress(h, MAKEINTRESOURCEA((WORD)ord));
        } else {
            func_ptr = (void*)GetProcAddress(h, func_name.c_str());
        }
        if (func_ptr) printf("%s!%s = 0x%llX\n", dll_name.c_str(), func_name.c_str(), (uint64_t)func_ptr);
        else throw std::runtime_error("Function not found\n");
    } else {
        throw std::runtime_error("DLL not found\n");
    }
    return (uint64_t)func_ptr;
}

static Layout handle_struct(ParseContext* ctx, std::string member_name) {
    // /struct { [TYPE] [NAME], ... } [--quiet]
    consume(ctx, TokenType::TOK_LBRACE, "Expected '{'");
    
    struct Mem { 
        std::string name; 
        std::string type_name; 
        int size, align, offset; 
    };
    std::vector<Mem> members;
    int offset = 0;
    int max_align = 1;

    while (peek(ctx).type != TokenType::TOK_RBRACE && peek(ctx).type != TokenType::TOK_EOF) {
        // 1. Variable capture ($var = ...)
        std::string var_capture = "";
        if (peek(ctx).type == TokenType::TOK_VARIABLE && peek(ctx, 1).type == TokenType::TOK_EQUALS) {
            var_capture = advance(ctx).text;
            advance(ctx); // consume '='
        }

        Mem m;
        // 2. Determine Type (either a primitive identifier or a nested struct)
        if (peek(ctx).type == TokenType::TOK_LBRACE) {
            // The "type" is a nested struct
            Layout nested = handle_struct(ctx, "nested"); 
            m.type_name = "struct";
            m.size = nested.size;
            m.align = nested.align;
        } else {
            // Standard primitive type
            Token type_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected Type");
            TypeKind tk = token_to_type(type_tok.text);
            m.type_name = type_tok.text;
            m.size = get_type_size(tk);
            m.align = m.size;
        }

        // 3. Member Name (comes after the type or after the '}')
        Token name_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected Member Name");
        m.name = name_tok.text;
        
        // 4. Calculate layout with alignment padding
        int pad = (offset % m.align == 0) ? 0 : (m.align - (offset % m.align));
        offset += pad;
        
        m.offset = offset;
        offset += m.size;
        if (m.align > max_align) max_align = m.align;
        
        if (!var_capture.empty()) {
            var_set(var_capture, { TypeKind::TYPE_U64, (uint64_t)m.offset });
        }

        members.push_back(m);
        if (!match(ctx, TokenType::TOK_COMMA)) break; 
    }
    
    consume(ctx, TokenType::TOK_RBRACE, "Expected '}'");
    
    // Tail padding: The total struct size must be a multiple of its max_align
    int total_size = (offset + max_align - 1) / max_align * max_align;

    if (member_name == "root") {
        // Handle --quiet flag only at the top level
        bool quiet = false;
        if (peek(ctx).type == TokenType::TOK_FLAG && advance(ctx).text == "--quiet") {
            quiet = true;
        }
        
        if (!quiet) {
            print("\nLayout: %s\n", member_name.c_str());
            print("Total Size: %d, Align: %d\n", total_size, max_align);
            print("%-8s | %-4s | %-12s | %s\n", "Offset", "Size", "Type", "Name");
            for(auto& m : members) {
                print("0x%04X   | %-4d | %-12s | %s\n", m.offset, m.size, m.type_name.c_str(), m.name.c_str());
            }
        }
    }

    return { (uint64_t)total_size, max_align };
}

// --- DLL Call Execution ---

static uint64_t execute_dll_call(CallSpec* spec, int line_info) {
    void* func_ptr = NULL;

    // If the name starts with 0x, it's a raw pointer, not a symbol name.
    if (spec->func_name[0] == '0' && (spec->func_name[1] == 'x' || spec->func_name[1] == 'X')) {
        func_ptr = (void*)strtoull(spec->func_name, NULL, 16);
    }
    else {
        // 2. DLL Symbol Lookup
        HMODULE h = NULL;
        if (strlen(spec->dll_path) > 0) {
            // Check registry first
            for(int i=0; i<g_registry_count; i++) {
                if (_stricmp(g_registry[i].path, spec->dll_path) == 0) {
                    h = g_registry[i].handle;
                    break;
                }
            }
            if (!h) h = LoadLibraryA(spec->dll_path);
        } else if (g_focus_idx >= 0) {
            h = g_registry[g_focus_idx].handle;
        }

        if (!h) throw SyntaxError("DLL not loaded/specified for: " + std::string(spec->func_name), line_info);

        // 3. Symbol Lookup (Name vs Ordinal)
        if (spec->func_name[0] == '#') {
            char* end_ptr;
            unsigned long ord = strtoul(spec->func_name + 1, &end_ptr, 10);
            
            // Validation:
            // 1. Check if we actually parsed digits (end_ptr moved)
            // 2. Check if it fits in a 16-bit WORD (Max 65535)
            if (end_ptr == spec->func_name + 1 || ord > 0xFFFF) {
                throw SyntaxError("Invalid ordinal: " + std::string(spec->func_name), line_info);
            }

            func_ptr = (void*)GetProcAddress(h, MAKEINTRESOURCEA((WORD)ord));
        } else {
            func_ptr = (void*)GetProcAddress(h, spec->func_name);
        }
    }

    if (!func_ptr) throw SyntaxError("Function not found: " + std::string(spec->func_name), line_info);

    // 3. Prepare Args
    uint64_t raw_args[16];
    uint32_t float_mask = 0;
    for(int i=0; i<spec->arg_count; i++) {
        raw_args[i] = spec->args[i].value;
        if (spec->args[i].type == TypeKind::TYPE_F32 || spec->args[i].type == TypeKind::TYPE_F64)
            float_mask |= (1 << i);
    }
    if (spec->return_type == TypeKind::TYPE_F32 || spec->return_type == TypeKind::TYPE_F64)
         float_mask |= 0x80000000;

    // 4. Call with SEH
    uint64_t result = 0;
    bool crashed = false;
    unsigned long code = 0;

    __try {
        result = call_dynamic_function(func_ptr, raw_args, spec->arg_count, float_mask);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        code = GetExceptionCode();
        crashed = true;
    }

    if (crashed) {
        printf("\n[!!!] CRASHED [!!!]\n");
        printf("Exception Code: 0x%08lX\n", code);
        
        switch(code) {
            case EXCEPTION_ACCESS_VIOLATION:    printf("Reason: Access Violation\n"); break;
            case EXCEPTION_STACK_OVERFLOW:      printf("Reason: Stack Overflow\n"); break;
            case EXCEPTION_ILLEGAL_INSTRUCTION: printf("Reason: Illegal Instruction\n"); break;
            case EXCEPTION_PRIV_INSTRUCTION:    printf("Reason: Privileged Instruction\n"); break;
        }
        
        throw std::runtime_error("DLL call crashed");
        return 0;
    }

    // 5. Flags & Assertions
    if (spec->print_result && spec->return_type != TypeKind::TYPE_VOID) {
        char buf[128];
        format_result(result, spec->return_type, buf, sizeof(buf));
        printf("Result: %s\n", buf);
    }

    g_assert_failed = false;
    if (spec->assert_type != AssertType::ASSERT_NONE) {
        bool fail = false;
        if (spec->assert_type == AssertType::ASSERT_ZERO && result != 0) fail = true;
        if (spec->assert_type == AssertType::ASSERT_NOT_ZERO && result == 0) fail = true;
        if (spec->assert_type == AssertType::ASSERT_NEGATIVE && (int64_t)result >= 0) fail = true;
        if (spec->assert_type == AssertType::ASSERT_NON_NEGATIVE && (int64_t)result < 0) fail = true;
        
        if (fail) {
            g_assert_failed = true;
            if (!g_in_a_loop)
                printf("Assertion Failed for result: %llu\n", result);
        }
    }

    return result;
}

// --- Main Dispatcher ---

static void parse_statement(ParseContext* ctx);

static void run_block(std::vector<Token>& tokens) {
    ParseContext sub = { tokens, 0 };
    while(sub.pos < sub.tokens.size()) {
        parse_statement(&sub);
        if (g_assert_failed) break; // Break loop on assert fail
    }
}

static void parse_statement(ParseContext* ctx) {
    if (peek(ctx).type == TokenType::TOK_EOF) return;

    if (peek(ctx).type == TokenType::TOK_COMMA) { advance(ctx); return; }

    // --- Directives ---
    if (peek(ctx).type == TokenType::TOK_SLASH) {
        advance(ctx);
        Token cmd = consume(ctx, TokenType::TOK_IDENTIFIER, "Expected command");
        
        if (cmd.text == "set") handle_set(ctx);
        else if (cmd.text == "get") handle_get(ctx);
        else if (cmd.text == "hex") handle_hex(ctx);
        else if (cmd.text == "address") handle_address(ctx);
        else if (cmd.text == "struct") handle_struct(ctx);
        else if (cmd.text == "loaddll") {
             std::string path = consume(ctx, TokenType::TOK_IDENTIFIER, "Path").text; 
             HMODULE h = LoadLibraryA(path.c_str());
             if(h) {
                 g_registry[g_registry_count].handle = h;
                 strcpy(g_registry[g_registry_count].path, path.c_str());
                 g_focus_idx = g_registry_count++;
                 printf("Loaded %s\n", path.c_str());
             }
        }
        else if (cmd.text == "dlls") {
             for(int i=0; i<g_registry_count; i++) 
                printf("%d: %s\n", i, g_registry[i].path);
        }
        else if (cmd.text == "quit") exit(0);
        else if (cmd.text == "for") {
            // /for <count> { ... }
            uint64_t count = parse_expression(ctx).value;
            consume(ctx, TokenType::TOK_LBRACE, "Expected '{'");
            
            std::vector<Token> body;
            int depth=1;
            while(depth > 0) {
                if(peek(ctx).type == TokenType::TOK_EOF) throw IncompleteInput();
                Token t = advance(ctx);
                if(t.type == TokenType::TOK_LBRACE) depth++;
                if(t.type == TokenType::TOK_RBRACE) depth--;
                if(depth > 0) body.push_back(t);
            }

            g_in_a_loop = true;
            for(uint64_t i=0; i<count; i++) {
                var_set("i", { TypeKind::TYPE_U64, i });
                run_block(body);
                if (g_assert_failed) {
                    g_assert_failed = false;
                    break;
                }
            }
            g_in_a_loop = false;
        }
        else if (cmd.text == "repeat") {
            // /repeat { ... }
            consume(ctx, TokenType::TOK_LBRACE, "Expected '{'");
            std::vector<Token> body;
            int depth=1;
            while(depth > 0) {
                if(peek(ctx).type == TokenType::TOK_EOF) throw IncompleteInput();
                Token t = advance(ctx);
                if(t.type == TokenType::TOK_LBRACE) depth++;
                if(t.type == TokenType::TOK_RBRACE) depth--;
                if(depth > 0) body.push_back(t);
            }

            g_in_a_loop = true;
            uint64_t i=0;
            while(true) {
                var_set("i", { TypeKind::TYPE_U64, i++ });
                run_block(body);
                if (g_assert_failed) {
                    g_assert_failed = false;
                    break;
                }
            }
            g_in_a_loop = false;
        }
        return;
    }

    // --- Assignment & Calls ---
    std::string assign_to = "";
    if (peek(ctx).type == TokenType::TOK_VARIABLE && peek(ctx, 1).type == TokenType::TOK_EQUALS) {
        assign_to = advance(ctx).text;
        advance(ctx); // eat =
    }

    // Call Detection
    bool is_call = false;
    bool var_call = false;
    int line_of_call = peek(ctx).line;

    // Case 1: DLL TYPE NAME ( ...
    if (peek(ctx).type == TokenType::TOK_IDENTIFIER && peek(ctx, 1).type == TokenType::TOK_IDENTIFIER && peek(ctx, 2).type == TokenType::TOK_IDENTIFIER) is_call = true;
    // Case 2: TYPE NAME ( ...
    if (peek(ctx).type == TokenType::TOK_IDENTIFIER && peek(ctx, 1).type == TokenType::TOK_IDENTIFIER && peek(ctx, 2).type == TokenType::TOK_LPAREN) is_call = true;
    // Case 3: TYPE $VAR ( ... [Variable Function Pointer]
    if (peek(ctx).type == TokenType::TOK_IDENTIFIER && peek(ctx, 1).type == TokenType::TOK_VARIABLE && peek(ctx, 2).type == TokenType::TOK_LPAREN) {
        is_call = true;
        var_call = true;
    }

    if (is_call) {
        CallSpec spec = {0};
        
        if (var_call) {
            // [TYPE] [$VAR] (
            spec.return_type = token_to_type(advance(ctx).text);
            Token vtok = advance(ctx);
            std::string vname = vtok.text; 
            line_of_call = vtok.line;
            
            // Resolve pointer now
            Value ptr; bool f;
            if (var_get(vname, &ptr)) {
                // Hack: store address as hex string so execute_dll_call can parse it
                sprintf(spec.func_name, "0x%llX", ptr.value);
            } else {
                throw SyntaxError("Unknown func pointer var: " + vname, vtok.line);
            }
        } else {
            // Standard: [DLL] [TYPE] [NAME]
            if (peek(ctx, 2).type == TokenType::TOK_IDENTIFIER) { 
                strncpy(spec.dll_path, advance(ctx).text.c_str(), 255);
            }
            spec.return_type = token_to_type(consume(ctx, TokenType::TOK_IDENTIFIER, "RetType").text);
            Token ftok = consume(ctx, TokenType::TOK_IDENTIFIER, "FuncName");
            strncpy(spec.func_name, ftok.text.c_str(), 127);
            line_of_call = ftok.line;
        }
        
        consume(ctx, TokenType::TOK_LPAREN, "Expected '('");
        
        while(peek(ctx).type != TokenType::TOK_RPAREN) {
            Token arg_type_tok = consume(ctx, TokenType::TOK_IDENTIFIER, "ArgType");
            spec.args[spec.arg_count].type = token_to_type(arg_type_tok.text);
            
            // Handle &Var (Address pass)
            if (peek(ctx).type == TokenType::TOK_AMP) {
                 spec.args[spec.arg_count].value = parse_expression(ctx).value;
            }

            // Handle Strings
            else if ((spec.args[spec.arg_count].type == TypeKind::TYPE_STR || 
                      spec.args[spec.arg_count].type == TypeKind::TYPE_WSTR) 
                     && peek(ctx).type == TokenType::TOK_STRING) {
                
                // 1. Concatenate adjacent string literals ("A" "B")
                std::string full = "";
                while(peek(ctx).type == TokenType::TOK_STRING) full += advance(ctx).text;
                
                // 2. Convert based on type
                if (spec.args[spec.arg_count].type == TypeKind::TYPE_WSTR) {
                    // Convert Narrow -> Wide
                    // CP_ACP uses system ANSI codepage (standard behavior for char*)
                    int req_chars = MultiByteToWideChar(CP_ACP, 0, full.c_str(), -1, NULL, 0);
                    wchar_t* wbuf = (wchar_t*)malloc(req_chars * sizeof(wchar_t));
                    MultiByteToWideChar(CP_ACP, 0, full.c_str(), -1, wbuf, req_chars);
                    spec.args[spec.arg_count].value = (uint64_t)wbuf;
                } else {
                    // Keep Narrow
                    spec.args[spec.arg_count].value = (uint64_t)strdup(full.c_str());
                }
            } 
            else {
                spec.args[spec.arg_count].value = parse_expression(ctx).value;
            }

            spec.arg_count++;
            if (!match(ctx, TokenType::TOK_COMMA)) break;
        }
        consume(ctx, TokenType::TOK_RPAREN, "Expected ')'");
        
        // Flags
        while(peek(ctx).type == TokenType::TOK_FLAG) {
            std::string f = advance(ctx).text;
            if (f == "--print-result") spec.print_result = 1;
            else if (f == "--assert=zero") spec.assert_type = AssertType::ASSERT_ZERO;
            else if (f == "--assert=nonzero") spec.assert_type = AssertType::ASSERT_NOT_ZERO;
            else if (f == "--assert=negative") spec.assert_type = AssertType::ASSERT_NEGATIVE;
            else if (f == "--assert=nonnegative") spec.assert_type = AssertType::ASSERT_NON_NEGATIVE;
        }
        
        uint64_t res = execute_dll_call(&spec, line_of_call);
        
        if (!assign_to.empty()) var_set(assign_to, { spec.return_type, res });
    
    } else {
        // Must be math
        Value res = parse_expression(ctx);
        if (!assign_to.empty()) var_set(assign_to, res);
        else {
            // Naked expression, just print
            char buf[64];
            format_result(res.value, res.type, buf, sizeof(buf));
            printf("= %s\n", buf);
        }
    }
}

void parse_and_execute(std::vector<Token>& tokens) {
    ParseContext ctx = { tokens, 0 };
    while(ctx.pos < ctx.tokens.size()) {
        if (peek(&ctx).type == TokenType::TOK_EOF) break;
        parse_statement(&ctx);
    }
}
