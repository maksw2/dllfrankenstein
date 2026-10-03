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

struct SemanticSnapshot {
    std::unordered_map<std::string, TypeId> named_types;
    std::unordered_map<std::string, TypeId> variables;
    std::unordered_map<std::string, FunctionSignature> functions;
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
