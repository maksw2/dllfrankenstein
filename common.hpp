#pragma once
#include <windows.h>
#include <map>
#include <cstdint>
#include <cstdio>

enum class TypeKind {
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_STR, TYPE_WSTR,
    TYPE_PTR, TYPE_VOID
};

enum class AssertType {
    ASSERT_NONE,
    ASSERT_ZERO,
    ASSERT_NOT_ZERO,
    ASSERT_NEGATIVE,
    ASSERT_NON_NEGATIVE
};

struct Argument {
    TypeKind type;
    uint64_t value;
};

struct CallSpec {
    char dll_path[256];
    char func_name[128];
    TypeKind return_type;
    AssertType assert_type;
    Argument args[16];
    int arg_count;
    int print_result;
};

struct Variable {
    char name[64];
    uint64_t value;
};

struct Member {
    char name[64];
    TypeKind type;
    int array_size;
    char struct_name[64];
    int offset;
    int size;
    int alignment;
};

struct Struct {
    Member members[128];
    int member_count;
    int total_size;
    int alignment;
};

struct RegisteredDLL {
    char path[256];
    HMODULE handle;
};

// god help me
extern Struct g_known_structs[256];
extern int g_known_struct_count;

extern Variable g_vars[128];
extern int g_var_count;

extern RegisteredDLL g_registry[32];
extern int g_registry_count;
extern int g_focus_idx;

extern bool g_interactive;
extern bool g_normal_vars;
extern bool g_quiet;
extern bool g_assert_failed;
extern bool g_in_a_loop;

// handlers.cpp
extern void handle_quit_command();
extern uint64_t handle_alloc_command(char* p);
extern void handle_free_command(char* p);
extern void handle_set_command(char* p);
extern void handle_memset_command(char* p);
extern uint64_t handle_get_command(char* p);
extern void handle_hex_command(char* p);
extern uint64_t handle_address_command(char* p);
extern size_t handle_struct_command(char* p);
extern void handle_loaddll_command(char* p);
extern void handle_freedll_command(char* p);
extern void handle_dlls_command();
extern uint64_t handle_function_call(char* input_line);
extern void handle_for_command(char* input_line);
extern void handle_repeat_until_command(char* input_line);

// caller.cpp
extern Variable* find_var(const char* name);
extern bool set_var(const char* name, uint64_t value);
extern void print(const char* format, ...);

// parser.cpp
extern char* skip_ws(char* s);
extern char* skip_ws_backwards(char* str, char* limit);
extern char* strndup(const char* s, size_t n);
extern uint64_t parse_argument_value(TypeKind type, const char* str);
extern TypeKind parse_type(const char* str);
extern char* read_token(char** s);
extern char* expand_vars(const char* input);
extern void format_result(uint64_t result, TypeKind type, char* buf, size_t size);
extern uint64_t get_operand_value(char* p, char** endptr);
extern void trim(char *str);
extern void get_type_info(TypeKind type, int *size, int *align);
extern int parse_header(char** s, char* ret_type, char* func_name);
extern void parse_arguments(char** s, CallSpec* spec);
extern void parse_flags(char* f_str, CallSpec* spec);
extern const char* type_to_string(TypeKind type);
extern uint64_t process_command(char* input_line);
