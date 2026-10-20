#pragma once
#include "ast.hpp"
#include "types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct FunctionSignature {
    TypeId return_type = INVALID_TYPE;
    std::vector<TypeId> parameters;
    SourceSpan span{};
};

enum class FfiOriginKind {
    Focused,
    External,
    Variable,
};

struct FfiBindingSignature {
    FunctionSignature signature;
    FfiOriginKind origin = FfiOriginKind::Focused;
    std::string module_name;
    std::string target_name;
    SourceSpan span{};
};

struct SemanticSnapshot {
    std::unordered_map<std::string, TypeId> named_types;
    std::unordered_map<std::string, TypeId> variables;
    std::unordered_map<std::string, FunctionSignature> functions;
    std::unordered_map<std::string, FfiBindingSignature> ffi_functions;
};

struct Warning {
    SourceSpan span{};
    std::string message;
};

struct CheckResult {
    SemanticSnapshot resulting_state;
    std::vector<Warning> warnings;
};

CheckResult check_module(Module& module, TypeRegistry& types, const SemanticSnapshot& initial);
