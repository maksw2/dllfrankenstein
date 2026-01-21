#pragma once
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdint>
#include "parser.hpp"

// Thrown when we hit EOF but expected more (e.g. missing ')' or '}')
struct IncompleteInput : public std::exception {};

// Thrown when the code is just wrong
struct SyntaxError : public std::runtime_error {
    int line;
    SyntaxError(const std::string& msg, int line_ = 0) 
        : std::runtime_error(msg), line(line_) {}
};

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
    ASSERT_NON_NEGATIVE,
    ASSERT_POSITIVE
};

struct Value {
    TypeKind type;
    uint64_t value;
};

struct CallSpec {
    char dll_path[256];
    char func_name[128];
    TypeKind return_type;
    AssertType assert_type;
    bool fatal;
    bool continue_on_fail;
    Value args[16];
    int arg_count;
    int print_result;
};

extern bool var_get(const std::string& name, Value* out_val);
extern void var_set(const std::string& name, Value val);
extern uint64_t var_get_addr(const std::string& name);
extern bool var_exists(const std::string& name);
extern void print(const char* format, ...);
