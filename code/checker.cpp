#include "checker.hpp"
#include <algorithm>
#include <optional>
#include <unordered_set>

namespace {
struct IntrinsicSignature {
    PrimitiveKind result;
    std::vector<PrimitiveKind> args;
};

std::optional<IntrinsicSignature> intrinsic_signature(const std::string& name) {
    if (name == "load") return IntrinsicSignature{PrimitiveKind::Void, {PrimitiveKind::Str}};
    if (name == "focus") return IntrinsicSignature{PrimitiveKind::Void, {PrimitiveKind::Str}};
    if (name == "free") return IntrinsicSignature{PrimitiveKind::Void, {PrimitiveKind::Str}};
    if (name == "dlls") return IntrinsicSignature{PrimitiveKind::Void, {}};
    if (name == "address") return IntrinsicSignature{PrimitiveKind::Ptr, {PrimitiveKind::Str, PrimitiveKind::Str}};
    if (name == "hex") return IntrinsicSignature{PrimitiveKind::Void, {PrimitiveKind::Ptr, PrimitiveKind::U64}};
    if (name == "quit") return IntrinsicSignature{PrimitiveKind::Void, {PrimitiveKind::I32}};
    return std::nullopt;
}

class Checker {
public:
    Checker(Module& module, TypeRegistry& types, const SemanticSnapshot& initial)
        : module_(module), types_(types), state_(initial) {
        variable_scopes_.push_back(state_.variables);
    }

    CheckResult run() {
        for (auto& stmt : module_.statements) check_statement(*stmt, true);
        state_.variables = variable_scopes_.front();
        return {state_, std::move(warnings_)};
    }

private:
    Module& module_;
    TypeRegistry& types_;
    SemanticSnapshot state_;
    std::vector<std::unordered_map<std::string, TypeId>> variable_scopes_;
    std::vector<Warning> warnings_;
    int loop_depth_ = 0;
    bool in_function_ = false;
    TypeId function_return_ = INVALID_TYPE;

    [[noreturn]] void fail(const std::string& message, SourceSpan span) const { throw DiagnosticError(message, span); }
    void warn(const std::string& message, SourceSpan span) { warnings_.push_back({span, message}); }

    TypeId lookup_variable(const std::string& name, SourceSpan span) const {
        for (auto it = variable_scopes_.rbegin(); it != variable_scopes_.rend(); ++it) {
            if (auto found = it->find(name); found != it->end()) return found->second;
        }
        fail("undefined variable '" + name + "'", span);
    }

    std::unordered_map<std::string, TypeId>* find_variable_scope(const std::string& name) {
        for (auto it = variable_scopes_.rbegin(); it != variable_scopes_.rend(); ++it) {
            if (it->contains(name)) return &*it;
        }
        return nullptr;
    }

    TypeId resolve_type(TypeSyntax& syntax) {
        if (syntax.resolved != INVALID_TYPE) return syntax.resolved;
        if (syntax.kind == TypeSyntax::Kind::Named) {
            TypeId id = types_.lookup_builtin(syntax.name);
            if (id == INVALID_TYPE) {
                auto it = state_.named_types.find(syntax.name);
                if (it == state_.named_types.end()) fail("unknown type '" + syntax.name + "'", syntax.span);
                id = it->second;
            }
            syntax.resolved = id;
            return id;
        }

        if (syntax.kind == TypeSyntax::Kind::Array) {
            TypeId element = resolve_type(*syntax.element_type);
            if (element == types_.primitive(PrimitiveKind::Void)) fail("array element type cannot be void", syntax.span);
            if (syntax.element_count_expr) {
                TypeId count = check_expression(*syntax.element_count_expr);
                if (!types_.is_integer(count)) fail("array size expression must be an integer", syntax.element_count_expr->span);
                syntax.resolved = types_.make_runtime_array(element);
                return syntax.resolved;
            }
            try { syntax.resolved = types_.make_array(element, syntax.element_count); }
            catch (const std::exception& e) { fail(e.what(), syntax.span); }
            return syntax.resolved;
        }

        std::vector<std::pair<std::string, TypeId>> fields;
        std::unordered_set<std::string> names;
        for (auto& field : syntax.fields) {
            if (!names.insert(field.name).second) fail("duplicate field '" + field.name + "'", field.span);
            TypeId field_type = resolve_type(*field.type);
            if (types_.get(field_type).runtime_sized)
                fail("runtime-sized arrays are not allowed in aggregate fields", field.span);
            fields.emplace_back(field.name, field_type);
        }
        const std::string debug = std::string(syntax.kind == TypeSyntax::Kind::AnonymousUnion ? "<anonymous union@" : "<anonymous struct@") +
                                  std::to_string(syntax.span.line) + ">";
        try {
            syntax.resolved = types_.make_aggregate(syntax.kind == TypeSyntax::Kind::AnonymousUnion, debug, syntax.pack, fields);
        } catch (const std::exception& e) {
            fail(e.what(), syntax.span);
        }
        return syntax.resolved;
    }

    TypeId resolve_field_type(FieldSyntax& field) {
        TypeId type = resolve_type(*field.type);
        if (types_.get(type).runtime_sized) fail("runtime-sized arrays are not allowed in aggregate fields", field.span);
        return type;
    }

    bool can_explicit_cast(TypeId from, TypeId to) const {
        if (from == to) return true;
        const auto a = types_.category(from), b = types_.category(to);
        if (a == TypeCategory::Void || b == TypeCategory::Void) return false;
        if (a == TypeCategory::Aggregate || b == TypeCategory::Aggregate) return false;
        return true;
    }

    void require_scalar(TypeId type, SourceSpan span, const char* operation) const {
        if (!types_.is_scalar(type)) fail(std::string(operation) + " requires a scalar value", span);
    }

    TypeId check_place(Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Variable:
                expr.checked_type = lookup_variable(expr.text, expr.span);
                return expr.checked_type;
            case ExprKind::Member: {
                TypeId base = check_place(*expr.lhs);
                const TypeInfo& info = types_.get(base);
                if (info.kind != RuntimeTypeKind::Struct && info.kind != RuntimeTypeKind::Union)
                    fail("member access requires a struct or union", expr.span);
                auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& f) { return f.name == expr.text; });
                if (it == info.fields.end()) fail("unknown field '" + expr.text + "' in " + types_.name(base), expr.span);
                expr.checked_type = it->type;
                return expr.checked_type;
            }
            case ExprKind::Index: {
                TypeId base = check_place(*expr.lhs);
                const TypeInfo& info = types_.get(base);
                if (info.kind != RuntimeTypeKind::Array) fail("indexing requires an array", expr.span);
                TypeId idx = check_expression(*expr.rhs);
                if (!types_.is_integer(idx)) fail("array index must be an integer", expr.rhs->span);
                expr.checked_type = info.element_type;
                return expr.checked_type;
            }
            case ExprKind::Deref: {
                TypeId address = check_expression(*expr.lhs);
                require_scalar(address, expr.lhs->span, "memory access");
                TypeId access = resolve_type(*expr.type);
                if (access == types_.primitive(PrimitiveKind::Void)) fail("memory access type cannot be void", expr.type->span);
                expr.checked_type = access;
                return expr.checked_type;
            }
            default: break;
        }
        fail("expression is not assignable", expr.span);
    }

    void check_call_arguments(Expr& expr, const FunctionSignature& signature, const std::string& display_name) {
        if (expr.args.size() != signature.parameters.size())
            fail("wrong number of arguments to '" + display_name + "'", expr.span);
        expr.checked_return_type = signature.return_type;
        expr.checked_arg_types = signature.parameters;
        for (size_t i = 0; i < expr.args.size(); ++i) {
            if (expr.args[i].explicitly_typed)
                fail("remembered calls use untyped arguments; provide a return type to change the signature", expr.args[i].span);
            TypeId actual = check_expression(*expr.args[i].value);
            if (!can_explicit_cast(actual, signature.parameters[i]))
                fail("argument " + std::to_string(i + 1) + " cannot be converted to " + types_.name(signature.parameters[i]), expr.args[i].span);
            expr.args[i].checked_type = signature.parameters[i];
        }
    }

    void apply_ffi_origin(Expr& expr, const FfiBindingSignature& binding) {
        expr.module_name.clear();
        expr.target_name = binding.target_name;
        switch (binding.origin) {
            case FfiOriginKind::Focused:
                expr.call_target = CallTargetKind::Named;
                break;
            case FfiOriginKind::External:
                expr.call_target = CallTargetKind::External;
                expr.module_name = binding.module_name;
                break;
            case FfiOriginKind::Variable:
                expr.call_target = CallTargetKind::Variable;
                break;
        }
    }

    void remember_ffi_call(const Expr& expr, CallTargetKind original_target, const std::string& original_module,
                           const std::string& original_name) {
        FfiBindingSignature binding;
        binding.signature.return_type = expr.checked_return_type;
        binding.signature.parameters = expr.checked_arg_types;
        binding.signature.span = expr.span;
        binding.target_name = original_name;
        binding.span = expr.span;
        if (original_target == CallTargetKind::External) {
            binding.origin = FfiOriginKind::External;
            binding.module_name = original_module;
        } else if (original_target == CallTargetKind::Variable) {
            binding.origin = FfiOriginKind::Variable;
        } else {
            binding.origin = FfiOriginKind::Focused;
        }
        state_.ffi_functions[original_name] = std::move(binding);
    }

    TypeId check_call(Expr& expr) {
        const CallTargetKind original_target = expr.call_target;
        const std::string original_module = expr.module_name;
        const std::string original_name = expr.target_name;

        auto intrinsic = expr.call_target == CallTargetKind::Named ? intrinsic_signature(expr.target_name) : std::nullopt;
        if (intrinsic && expr.module_name.empty()) {
            expr.checked_intrinsic_target = true;
            const TypeId intrinsic_return = types_.primitive(intrinsic->result);
            if (expr.return_type) {
                expr.checked_return_type = resolve_type(*expr.return_type);
                if (expr.checked_return_type != intrinsic_return) fail("intrinsic return type is " + types_.name(intrinsic_return), expr.return_type->span);
            } else expr.checked_return_type = intrinsic_return;
            const bool optional_exit_code = expr.target_name == "quit";
            if ((!optional_exit_code && expr.args.size() != intrinsic->args.size()) ||
                (optional_exit_code && expr.args.size() > 1))
                fail("wrong number of arguments to intrinsic '" + expr.target_name + "'", expr.span);
            for (size_t i = 0; i < expr.args.size(); ++i) {
                TypeId expected = types_.primitive(intrinsic->args[i]);
                TypeId actual;
                if (expr.args[i].explicitly_typed) {
                    TypeId stated = resolve_type(*expr.args[i].type);
                    if (stated != expected) fail("intrinsic argument type must be " + types_.name(expected), expr.args[i].span);
                    actual = check_expression(*expr.args[i].value);
                    if (!can_explicit_cast(actual, stated)) fail("cannot cast intrinsic argument", expr.args[i].span);
                } else {
                    actual = check_expression(*expr.args[i].value);
                    if (!can_explicit_cast(actual, expected)) fail("intrinsic argument must be convertible to " + types_.name(expected), expr.args[i].span);
                }
                expr.args[i].checked_type = expected;
                expr.checked_arg_types.push_back(expected);
            }
            expr.checked_type = expr.checked_return_type;
            return expr.checked_type;
        }

        const FunctionSignature* script = nullptr;
        if (expr.call_target == CallTargetKind::Named && expr.module_name.empty()) {
            if (auto it = state_.functions.find(expr.target_name); it != state_.functions.end()) script = &it->second;
        }

        const bool args_typed = !expr.args.empty() && expr.args.front().explicitly_typed;
        const bool has_explicit_return = expr.return_type != nullptr;
        const bool untyped_call = !expr.explicit_signature && !args_typed && !has_explicit_return;

        if (untyped_call) {
            // A script function with the same bare name always wins over a remembered
            // FFI binding. Function-pointer calls have their own `$name` namespace.
            if (script) {
                expr.checked_script_target = true;
                check_call_arguments(expr, *script, expr.target_name);
            } else {
                auto known = state_.ffi_functions.find(original_name);
                if (known == state_.ffi_functions.end()) fail("no known function '" + original_name + "'", expr.span);
                apply_ffi_origin(expr, known->second);
                if (expr.call_target == CallTargetKind::Variable) {
                    const TypeId target_type = lookup_variable(expr.target_name, expr.span);
                    require_scalar(target_type, expr.span, "function pointer target");
                }
                check_call_arguments(expr, known->second.signature, original_name);
            }
        } else {
            // Explicitly typed calls still use a same-name script function when one
            // is already known. Otherwise a complete signature declares/redeclares
            // the remembered FFI binding for this name and origin.
            if (expr.return_type) expr.checked_return_type = resolve_type(*expr.return_type);
            else {
                if (!script) fail("typed arguments without an explicit return type require a script function", expr.span);
                expr.checked_return_type = script->return_type;
            }
            if (!abi_type_supported(types_, expr.checked_return_type, true)) fail("call return type is not supported by the Win64 scalar ABI bridge", expr.span);
            for (auto& arg : expr.args) {
                if (!arg.explicitly_typed) fail("cannot mix typed and untyped call arguments", arg.span);
                arg.checked_type = resolve_type(*arg.type);
                if (!abi_type_supported(types_, arg.checked_type, false)) fail("call argument type is not supported by the Win64 scalar ABI bridge", arg.span);
                TypeId actual = check_expression(*arg.value);
                if (!can_explicit_cast(actual, arg.checked_type)) fail("cannot cast explicit call argument to " + types_.name(arg.checked_type), arg.span);
                expr.checked_arg_types.push_back(arg.checked_type);
            }
            expr.checked_script_target = script != nullptr && expr.call_target == CallTargetKind::Named;

            if (expr.call_target == CallTargetKind::Variable) {
                const TypeId target_type = lookup_variable(expr.target_name, expr.span);
                require_scalar(target_type, expr.span, "function pointer target");
            }

            if (!expr.checked_script_target && has_explicit_return)
                remember_ffi_call(expr, original_target, original_module, original_name);
        }
        expr.checked_type = expr.checked_return_type;
        return expr.checked_type;
    }

    TypeId check_expression(Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Integer: expr.checked_type = types_.primitive(PrimitiveKind::I64); break;
            case ExprKind::Float: expr.checked_type = types_.primitive(PrimitiveKind::F32); break;
            case ExprKind::String: expr.checked_type = types_.primitive(PrimitiveKind::Str); break;
            case ExprKind::Variable: expr.checked_type = lookup_variable(expr.text, expr.span); break;
            case ExprKind::FunctionAddress: {
                if (!state_.functions.contains(expr.text)) fail("unknown script function '" + expr.text + "'", expr.span);
                expr.checked_type = types_.primitive(PrimitiveKind::Ptr);
                break;
            }
            case ExprKind::Unary: {
                if (expr.op == TokenKind::Amp) {
                    check_place(*expr.lhs);
                    expr.checked_type = types_.primitive(PrimitiveKind::Ptr);
                    break;
                }
                TypeId operand = check_expression(*expr.lhs);
                if (expr.op == TokenKind::Bang) {
                    require_scalar(operand, expr.span, "logical not");
                    expr.checked_type = types_.primitive(PrimitiveKind::I64);
                } else if (expr.op == TokenKind::Tilde) {
                    if (!types_.is_integer(operand)) fail("bitwise not requires an integer", expr.span);
                    expr.checked_type = types_.primitive(PrimitiveKind::I64);
                } else if (expr.op == TokenKind::Minus) {
                    if (!types_.is_integer(operand) && !types_.is_float(operand)) fail("unary minus requires a numeric value", expr.span);
                    expr.checked_type = operand;
                }
                break;
            }
            case ExprKind::Deref:
                expr.checked_type = check_place(expr);
                break;
            case ExprKind::Binary: {
                TypeId a = check_expression(*expr.lhs), b = check_expression(*expr.rhs);
                switch (expr.op) {
                    case TokenKind::EqualEqual: case TokenKind::BangEqual:
                    case TokenKind::Less: case TokenKind::LessEqual: case TokenKind::Greater: case TokenKind::GreaterEqual:
                        require_scalar(a, expr.lhs->span, "comparison"); require_scalar(b, expr.rhs->span, "comparison");
                        expr.checked_type = types_.primitive(PrimitiveKind::I64); break;
                    case TokenKind::AmpAmp: case TokenKind::PipePipe:
                        require_scalar(a, expr.lhs->span, "logical operator"); require_scalar(b, expr.rhs->span, "logical operator");
                        expr.checked_type = types_.primitive(PrimitiveKind::I64); break;
                    case TokenKind::Amp: case TokenKind::Pipe: case TokenKind::Caret:
                    case TokenKind::ShiftLeft: case TokenKind::ShiftRight:
                        if (!types_.is_integer(a) || !types_.is_integer(b)) fail("bitwise operators require integers", expr.span);
                        expr.checked_type = types_.primitive(PrimitiveKind::I64); break;
                    case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent: {
                        const TypeId ptr = types_.primitive(PrimitiveKind::Ptr);
                        const bool a_ptr = a == ptr, b_ptr = b == ptr;

                        if (a_ptr || b_ptr) {
                            // ptr arithmetic is deliberately untyped: offsets and differences are always bytes.
                            if (expr.op == TokenKind::Plus && ((a_ptr && types_.is_integer(b)) || (b_ptr && types_.is_integer(a)))) {
                                expr.checked_type = ptr;
                                break;
                            }
                            if (expr.op == TokenKind::Minus && a_ptr && types_.is_integer(b)) {
                                expr.checked_type = ptr;
                                break;
                            }
                            if (expr.op == TokenKind::Minus && a_ptr && b_ptr) {
                                expr.checked_type = types_.primitive(PrimitiveKind::I64);
                                break;
                            }
                            fail("pointer arithmetic supports ptr + integer, integer + ptr, ptr - integer, and ptr - ptr", expr.span);
                        }

                        if ((!types_.is_integer(a) && !types_.is_float(a)) || (!types_.is_integer(b) && !types_.is_float(b)))
                            fail("arithmetic requires numeric operands", expr.span);
                        if (expr.op == TokenKind::Percent && (types_.is_float(a) || types_.is_float(b))) fail("remainder requires integers", expr.span);
                        if (a == types_.primitive(PrimitiveKind::F64) || b == types_.primitive(PrimitiveKind::F64)) expr.checked_type = types_.primitive(PrimitiveKind::F64);
                        else if (types_.is_float(a) || types_.is_float(b)) expr.checked_type = types_.primitive(PrimitiveKind::F32);
                        else expr.checked_type = types_.primitive(PrimitiveKind::I64);
                        break;
                    }
                    default: fail("unsupported binary operator", expr.span);
                }
                break;
            }
            case ExprKind::Member:
            case ExprKind::Index:
                expr.checked_type = check_place(expr); break;
            case ExprKind::Call:
                return check_call(expr);
            case ExprKind::TypedValue: {
                TypeId target = resolve_type(*expr.type);
                TypeId source = check_expression(*expr.lhs);
                if (!can_explicit_cast(source, target)) fail("cannot cast " + types_.name(source) + " to " + types_.name(target), expr.span);
                expr.checked_type = target;
                break;
            }
            case ExprKind::Aggregate: {
                TypeId aggregate = resolve_type(*expr.type);
                const TypeInfo& info = types_.get(aggregate);
                if (info.kind != RuntimeTypeKind::Struct && info.kind != RuntimeTypeKind::Union) fail("initializer requires a struct or union type", expr.span);
                std::unordered_set<std::string> seen;
                for (auto& init : expr.initializers) {
                    if (!seen.insert(init.name).second) fail("duplicate initializer for field '" + init.name + "'", init.span);
                    auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& f) { return f.name == init.name; });
                    if (it == info.fields.end()) fail("unknown field '" + init.name + "' in " + types_.name(aggregate), init.span);
                    TypeId actual = check_expression(*init.value);
                    if (!type_can_assign(types_, actual, it->type) && !can_explicit_cast(actual, it->type))
                        fail("cannot initialize field '" + init.name + "' with " + types_.name(actual), init.span);
                    init.value->checked_type = actual;
                }
                expr.checked_type = aggregate;
                break;
            }
            case ExprKind::Array: {
                TypeId array = resolve_type(*expr.type);
                const TypeInfo& info = types_.get(array);
                if (info.kind != RuntimeTypeKind::Array) fail("array initializer requires an array type", expr.span);
                if (!expr.type->element_count_expr && expr.elements.size() > info.element_count)
                    fail("too many elements in " + types_.name(array) + " initializer", expr.span);
                for (auto& element : expr.elements) {
                    TypeId actual = check_expression(*element);
                    if (!type_can_assign(types_, actual, info.element_type) && !can_explicit_cast(actual, info.element_type))
                        fail("cannot initialize " + types_.name(info.element_type) + " array element with " + types_.name(actual), element->span);
                }
                expr.checked_type = array;
                break;
            }
            case ExprKind::SizeOf:
            case ExprKind::AlignOf: {
                if (expr.type) resolve_type(*expr.type);
                else check_expression(*expr.lhs);
                expr.checked_type = types_.primitive(PrimitiveKind::U64);
                break;
            }
            case ExprKind::OffsetOf: {
                TypeId base = resolve_type(*expr.type);
                try { types_.field_offset(base, expr.member_path); }
                catch (const std::exception& e) { fail(e.what(), expr.span); }
                expr.checked_type = types_.primitive(PrimitiveKind::U64);
                break;
            }
        }
        return expr.checked_type;
    }

    void check_aggregate_definition(Stmt& stmt) {
        std::vector<std::pair<std::string, TypeId>> fields;
        std::unordered_set<std::string> names;
        for (auto& field : stmt.fields) {
            if (!names.insert(field.name).second) fail("duplicate field '" + field.name + "'", field.span);
            fields.emplace_back(field.name, resolve_field_type(field));
        }
        const bool is_union = stmt.kind == StmtKind::UnionDefinition;
        try { stmt.defined_type = types_.make_aggregate(is_union, stmt.name, stmt.pack, fields); }
        catch (const std::exception& e) { fail(e.what(), stmt.span); }
        if (state_.named_types.contains(stmt.name)) warn("redefining type '" + stmt.name + "'", stmt.span);
        state_.named_types[stmt.name] = stmt.defined_type;
    }

    bool definitely_returns(const Stmt& stmt) const {
        switch (stmt.kind) {
            case StmtKind::Return: return true;
            case StmtKind::Block:
                for (const auto& child : stmt.body) if (definitely_returns(*child)) return true;
                return false;
            case StmtKind::If: {
                if (!stmt.else_branch) return false;
                bool then_returns = false;
                for (const auto& child : stmt.body) { if (definitely_returns(*child)) { then_returns = true; break; } }
                return then_returns && definitely_returns(*stmt.else_branch);
            }
            case StmtKind::Switch: {
                bool has_default = false;
                if (stmt.switch_arms.empty()) return false;
                for (const auto& arm : stmt.switch_arms) {
                    has_default = has_default || arm.is_default;
                    if (!body_definitely_returns(arm.body)) return false;
                }
                return has_default;
            }
            default: return false;
        }
    }

    bool body_definitely_returns(const std::vector<std::unique_ptr<Stmt>>& body) const {
        for (const auto& stmt : body) if (definitely_returns(*stmt)) return true;
        return false;
    }

    void check_function(Stmt& stmt) {
        TypeId ret = resolve_type(*stmt.return_type);
        if (!abi_type_supported(types_, ret, true)) fail("script function return type must be a scalar ABI type", stmt.return_type->span);
        stmt.checked_return_type = ret;
        FunctionSignature signature;
        signature.return_type = ret;
        signature.span = stmt.span;
        for (auto& p : stmt.parameters) {
            p.checked_type = resolve_type(*p.type);
            if (!abi_type_supported(types_, p.checked_type, false)) fail("script function parameters must be scalar ABI types", p.span);
            signature.parameters.push_back(p.checked_type);
        }
        if (state_.functions.contains(stmt.name)) warn("redefining function '" + stmt.name + "' (same trampoline slot will be reused)", stmt.span);
        state_.functions[stmt.name] = signature; // visible to its own body for recursion

        auto saved_scopes = variable_scopes_;
        const bool saved_in_function = in_function_;
        const TypeId saved_return = function_return_;
        const int saved_loop = loop_depth_;
        // Snapshot the globals visible at this point in the current module.
        // state_.variables only contains globals from the incoming semantic snapshot;
        // top-level assignments checked earlier in this module live in
        // variable_scopes_.front() until run() finishes.
        const auto visible_globals = variable_scopes_.front();
        variable_scopes_.clear();
        variable_scopes_.push_back(visible_globals); // body checking cannot mutate outer semantic state
        variable_scopes_.push_back({});
        for (const auto& p : stmt.parameters) {
            if (variable_scopes_.front().contains(p.name)) fail("parameter '" + p.name + "' would shadow a global variable; rename it", p.span);
            if (variable_scopes_.back().contains(p.name)) fail("duplicate parameter '" + p.name + "'", p.span);
            variable_scopes_.back()[p.name] = p.checked_type;
        }
        in_function_ = true;
        function_return_ = ret;
        loop_depth_ = 0;
        for (auto& s : stmt.body) check_statement(*s, false);
        if (ret != types_.primitive(PrimitiveKind::Void) && !body_definitely_returns(stmt.body))
            fail("non-void function may reach the end without returning", stmt.span);
        variable_scopes_ = std::move(saved_scopes);
        in_function_ = saved_in_function;
        function_return_ = saved_return;
        loop_depth_ = saved_loop;
    }

    void check_assignment(Stmt& stmt) {
        TypeId value = check_expression(*stmt.value);
        if (stmt.target->kind == ExprKind::Variable) {
            auto* scope = find_variable_scope(stmt.target->text);
            if (!scope) {
                variable_scopes_.back()[stmt.target->text] = value;
            } else {
                const TypeId old = (*scope)[stmt.target->text];
                const bool scalar_retype = types_.is_scalar(old) && types_.is_scalar(value);
                if (!scalar_retype && old != value)
                    fail("cannot change variable '" + stmt.target->text + "' storage from " + types_.name(old) + " to " + types_.name(value), stmt.span);
                (*scope)[stmt.target->text] = value;
            }
            stmt.target->checked_type = value;
            return;
        }
        TypeId target = check_place(*stmt.target);
        if (!type_can_assign(types_, value, target) && !can_explicit_cast(value, target))
            fail("cannot assign " + types_.name(value) + " to " + types_.name(target), stmt.span);
    }

    void check_statement(Stmt& stmt, bool top_level) {
        switch (stmt.kind) {
            case StmtKind::StructDefinition:
            case StmtKind::UnionDefinition:
                if (!top_level) fail("named aggregate definitions are only allowed at top level", stmt.span);
                check_aggregate_definition(stmt); break;
            case StmtKind::FunctionDefinition:
                if (!top_level) fail("function definitions are only allowed at top level", stmt.span);
                check_function(stmt); break;
            case StmtKind::Assignment: check_assignment(stmt); break;
            case StmtKind::Call: check_expression(*stmt.expr); break;
            case StmtKind::Block:
                variable_scopes_.push_back({});
                for (auto& s : stmt.body) check_statement(*s, false);
                variable_scopes_.pop_back();
                break;
            case StmtKind::If:
                if (!types_.is_scalar(check_expression(*stmt.expr))) fail("if condition must be scalar", stmt.expr->span);
                variable_scopes_.push_back({});
                for (auto& s : stmt.body) check_statement(*s, false);
                variable_scopes_.pop_back();
                if (stmt.else_branch) check_statement(*stmt.else_branch, false);
                break;
            case StmtKind::Switch: {
                const TypeId subject = check_expression(*stmt.expr);
                if (!types_.is_scalar(subject)) fail("switch expression must be scalar", stmt.expr->span);

                // Case expressions are runtime expressions. Check all of them against
                // the switch subject before checking any arm bodies, since an
                // unmatched arm body cannot affect evaluation of later cases.
                for (auto& arm : stmt.switch_arms) {
                    for (auto& value : arm.values) {
                        const TypeId case_type = check_expression(*value);
                        if (!types_.is_scalar(case_type)) fail("case expression must be scalar", value->span);
                        if (!can_explicit_cast(case_type, subject))
                            fail("case expression cannot be converted to " + types_.name(subject), value->span);
                    }
                }

                const auto before_switch = variable_scopes_;
                for (auto& arm : stmt.switch_arms) {
                    variable_scopes_ = before_switch;
                    variable_scopes_.push_back({});
                    for (auto& s : arm.body) check_statement(*s, false);
                    variable_scopes_.pop_back();
                }
                variable_scopes_ = before_switch;
                break;
            }
            case StmtKind::While:
                if (!types_.is_scalar(check_expression(*stmt.expr))) fail("while condition must be scalar", stmt.expr->span);
                ++loop_depth_;
                variable_scopes_.push_back({});
                for (auto& s : stmt.body) check_statement(*s, false);
                variable_scopes_.pop_back();
                --loop_depth_;
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                if (loop_depth_ == 0) fail(std::string(stmt.kind == StmtKind::Break ? "break" : "continue") + " used outside a loop", stmt.span);
                break;
            case StmtKind::Return:
                if (!in_function_) fail("return used outside a function", stmt.span);
                if (function_return_ == types_.primitive(PrimitiveKind::Void)) {
                    if (stmt.expr) fail("void function cannot return a value", stmt.span);
                } else {
                    if (!stmt.expr) fail("non-void function must return a value", stmt.span);
                    TypeId actual = check_expression(*stmt.expr);
                    if (!can_explicit_cast(actual, function_return_)) fail("return value cannot be converted to " + types_.name(function_return_), stmt.span);
                }
                break;
            case StmtKind::Assert:
                if (!types_.is_scalar(check_expression(*stmt.expr))) fail("assert expression must be scalar", stmt.expr->span);
                break;
        }
    }
};
}

CheckResult check_module(Module& module, TypeRegistry& types, const SemanticSnapshot& initial) {
    Checker checker(module, types, initial);
    return checker.run();
}
