#pragma once
#include "lexer.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using TypeId = uint32_t;
constexpr TypeId INVALID_TYPE = UINT32_MAX;

struct TypeSyntax;
struct Expr;
struct Stmt;

struct FieldSyntax {
    std::unique_ptr<TypeSyntax> type;
    std::string name;
    SourceSpan span{};
};

struct TypeSyntax {
    enum class Kind { Named, AnonymousStruct, AnonymousUnion, Array } kind = Kind::Named;
    std::string name;
    std::vector<FieldSyntax> fields;
    std::unique_ptr<TypeSyntax> element_type;
    uint32_t element_count = 0;
    uint32_t pack = 8;
    bool explicit_pack = false;
    SourceSpan span{};
    TypeId resolved = INVALID_TYPE;
};

struct InitField {
    std::string name;
    std::unique_ptr<Expr> value;
    SourceSpan span{};
};

enum class ExprKind {
    Integer,
    Float,
    String,
    Variable,
    Unary,
    Deref,
    Binary,
    Member,
    Index,
    Call,
    TypedValue,
    Aggregate,
    Array,
    SizeOf,
    AlignOf,
    OffsetOf,
    FunctionAddress,
};

enum class CallTargetKind { Named, Variable, External };

struct CallArg {
    bool explicitly_typed = false;
    std::unique_ptr<TypeSyntax> type;
    std::unique_ptr<Expr> value;
    TypeId checked_type = INVALID_TYPE;
    SourceSpan span{};
};

struct Expr {
    ExprKind kind = ExprKind::Integer;
    SourceSpan span{};
    TypeId checked_type = INVALID_TYPE;

    uint64_t integer = 0;
    double floating = 0.0;
    std::string text;
    TokenKind op = TokenKind::Eof;

    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;

    // Call.
    CallTargetKind call_target = CallTargetKind::Named;
    std::string module_name;
    std::string target_name;
    bool explicit_signature = false;
    std::unique_ptr<TypeSyntax> return_type;
    std::vector<CallArg> args;
    TypeId checked_return_type = INVALID_TYPE;
    std::vector<TypeId> checked_arg_types;
    bool checked_script_target = false;
    bool checked_intrinsic_target = false;

    // Typed value / aggregate / sizeof / alignof / offsetof.
    std::unique_ptr<TypeSyntax> type;
    std::vector<InitField> initializers;
    std::vector<std::unique_ptr<Expr>> elements;
    std::vector<std::string> member_path;
};

enum class StmtKind {
    Assignment,
    Call,
    Block,
    If,
    While,
    Break,
    Continue,
    Return,
    Assert,
    StructDefinition,
    UnionDefinition,
    FunctionDefinition,
};

struct ParameterSyntax {
    std::unique_ptr<TypeSyntax> type;
    std::string name;
    TypeId checked_type = INVALID_TYPE;
    SourceSpan span{};
};

struct Stmt {
    StmtKind kind = StmtKind::Block;
    SourceSpan span{};

    // Assignment.
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    // Call / return / assert / conditions.
    std::unique_ptr<Expr> expr;

    // Block and control flow.
    std::vector<std::unique_ptr<Stmt>> body;
    std::unique_ptr<Stmt> else_branch;

    // Named aggregate definition.
    std::string name;
    std::vector<FieldSyntax> fields;
    uint32_t pack = 8;
    bool explicit_pack = false;
    TypeId defined_type = INVALID_TYPE;

    // Function definition.
    std::unique_ptr<TypeSyntax> return_type;
    TypeId checked_return_type = INVALID_TYPE;
    std::vector<ParameterSyntax> parameters;
};

struct Module {
    std::string source_name;
    std::string source;
    std::vector<Token> tokens;
    std::vector<std::unique_ptr<Stmt>> statements;
};
