#include "interpreter.hpp"
#include "ffi.hpp"
#include "platform.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {
struct Place {
    TypeId type = INVALID_TYPE;
    void* address = nullptr;
    Binding* binding = nullptr;
};

enum class FlowKind { Normal, Break, Continue, Return };
struct Flow {
    FlowKind kind = FlowKind::Normal;
    RuntimeValue value{};
};

struct ThreadState {
    Runtime* runtime = nullptr;
    uint32_t invocation_depth = 0;
};
thread_local ThreadState g_thread_state;

class ThreadInvocationScope {
public:
    explicit ThreadInvocationScope(Runtime& runtime) {
        if (g_thread_state.runtime && g_thread_state.runtime != &runtime) throw std::runtime_error("OS thread is already attached to another runtime");
        if (g_thread_state.invocation_depth == 0) platform_prepare_thread_exception_handling();
        g_thread_state.runtime = &runtime;
        ++g_thread_state.invocation_depth;
    }
    ~ThreadInvocationScope() {
        if (--g_thread_state.invocation_depth == 0) g_thread_state.runtime = nullptr;
    }
};

class Executor {
public:
    Executor(Runtime& runtime, bool module_root, bool echo, TypeId function_return = INVALID_TYPE)
        : runtime_(runtime), types_(runtime.types()), module_root_(module_root), echo_(echo), function_return_(function_return) {
        scopes_.push_back({});
    }

    void bind_parameter(const std::string& name, TypeId type, RuntimeValue value) {
        Binding* binding = create_local(name, type);
        store_binding(*binding, cast(value, type));
    }

    Flow run(const std::vector<std::unique_ptr<Stmt>>& statements) {
        for (auto& stmt : statements) {
            Flow flow = execute(*stmt);
            if (flow.kind != FlowKind::Normal) return flow;
        }
        return {};
    }

private:
    Runtime& runtime_;
    TypeRegistry& types_;
    bool module_root_ = false;
    bool echo_ = false;
    TypeId function_return_ = INVALID_TYPE;
    FrameArena arena_;
    std::vector<std::unordered_map<std::string, Binding*>> scopes_;
    std::vector<std::unique_ptr<Binding>> bindings_;

    Flow run_scoped(const std::vector<std::unique_ptr<Stmt>>& statements) {
        scopes_.push_back({});
        try {
            Flow flow = run(statements);
            scopes_.pop_back();
            return flow;
        } catch (...) {
            scopes_.pop_back();
            throw;
        }
    }

    const TypeField& require_field(TypeId aggregate, const char* name) const {
        const TypeInfo& info = types_.get(aggregate);
        auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& field) { return field.name == name; });
        if (it == info.fields.end()) throw std::runtime_error(std::string("missing builtin exception field '") + name + "'");
        return *it;
    }

    void* field_address(void* aggregate, TypeId type, const char* name) const {
        const TypeField& field = require_field(type, name);
        return static_cast<std::byte*>(aggregate) + field.offset;
    }

    uint64_t read_scalar_bytes(const void* address, TypeId type) const {
        const TypeInfo& info = types_.get(type);
        uint64_t bits = 0;
        PlatformNativeException fault;
        if (!platform_read_memory(address, &bits, info.size, &fault)) throw NativeFault(fault);
        return normalize_bits(bits, type);
    }

    uint64_t read_scalar_field(const void* aggregate, TypeId type, const char* name) const {
        const TypeField& field = require_field(type, name);
        return read_scalar_bytes(static_cast<const std::byte*>(aggregate) + field.offset, field.type);
    }

    void write_scalar_field(void* aggregate, TypeId type, const char* name, uint64_t bits) {
        const TypeField& field = require_field(type, name);
        const TypeInfo& info = types_.get(field.type);
        bits = mask_width(bits, info.size);
        std::memcpy(static_cast<std::byte*>(aggregate) + field.offset, &bits, info.size);
    }

    RuntimeValue make_native_exception_value(const PlatformNativeException& snapshot) {
        const TypeId exception_type = types_.lookup_builtin("NATIVE_EXCEPTION");
        const TypeId record_type = types_.lookup_builtin("EXCEPTION_RECORD");
        const TypeId context_type = types_.lookup_builtin("CONTEXT");
        const TypeInfo& exception_info = types_.get(exception_type);
        const TypeInfo& record_info = types_.get(record_type);
        const TypeInfo& context_info = types_.get(context_type);
        if (context_info.size != sizeof(PlatformContextX64)) throw std::runtime_error("CONTEXT mirror layout mismatch");

        void* memory = arena_.allocate(exception_info.size, exception_info.alignment);
        std::memset(memory, 0, exception_info.size);
        void* root_record = field_address(memory, exception_type, "record");

        void* records[PLATFORM_EXCEPTION_MAX_RECORDS]{};
        const uint32_t count = std::min(snapshot.record_count, PLATFORM_EXCEPTION_MAX_RECORDS);
        if (count) records[0] = root_record;
        for (uint32_t i = 1; i < count; ++i) {
            records[i] = runtime_.allocate_persistent(record_info.size, record_info.alignment);
            std::memset(records[i], 0, record_info.size);
        }

        const TypeField& information_field = require_field(record_type, "ExceptionInformation");
        const TypeInfo& information_type = types_.get(information_field.type);
        for (uint32_t i = 0; i < count; ++i) {
            const PlatformExceptionRecord& source = snapshot.records[i];
            void* destination = records[i];
            write_scalar_field(destination, record_type, "ExceptionCode", source.ExceptionCode);
            write_scalar_field(destination, record_type, "ExceptionFlags", source.ExceptionFlags);
            write_scalar_field(destination, record_type, "ExceptionRecord",
                               i + 1 < count ? reinterpret_cast<uint64_t>(records[i + 1]) : 0);
            write_scalar_field(destination, record_type, "ExceptionAddress", source.ExceptionAddress);
            write_scalar_field(destination, record_type, "NumberParameters", source.NumberParameters);
            std::memcpy(static_cast<std::byte*>(destination) + information_field.offset,
                        source.ExceptionInformation, std::min<size_t>(information_type.size, sizeof(source.ExceptionInformation)));
        }

        void* context = field_address(memory, exception_type, "context");
        std::memcpy(context, &snapshot.context, sizeof(snapshot.context));
        write_scalar_field(memory, exception_type, "thread_id", snapshot.thread_id);
        write_scalar_field(memory, exception_type, "stack_recovered", snapshot.stack_recovered ? 1 : 0);
        write_scalar_field(memory, exception_type, "record_count", count);
        write_scalar_field(memory, exception_type, "record_chain_truncated", snapshot.record_chain_truncated ? 1 : 0);
        return {exception_type, 0, memory};
    }

    PlatformNativeException snapshot_from_native_exception_value(RuntimeValue value) {
        const TypeId exception_type = types_.lookup_builtin("NATIVE_EXCEPTION");
        const TypeId record_type = types_.lookup_builtin("EXCEPTION_RECORD");
        const TypeId context_type = types_.lookup_builtin("CONTEXT");
        if (value.type != exception_type || !value.aggregate) throw std::runtime_error("throw value is not a NATIVE_EXCEPTION");
        if (types_.get(context_type).size != sizeof(PlatformContextX64)) throw std::runtime_error("CONTEXT mirror layout mismatch");

        PlatformNativeException snapshot;
        snapshot.thread_id = static_cast<uint32_t>(read_scalar_field(value.aggregate, exception_type, "thread_id"));
        snapshot.stack_recovered = read_scalar_field(value.aggregate, exception_type, "stack_recovered") != 0;
        snapshot.record_chain_truncated = read_scalar_field(value.aggregate, exception_type, "record_chain_truncated") != 0;
        const uint32_t requested_count = static_cast<uint32_t>(read_scalar_field(value.aggregate, exception_type, "record_count"));
        snapshot.record_count = std::min(requested_count, PLATFORM_EXCEPTION_MAX_RECORDS);
        if (requested_count > PLATFORM_EXCEPTION_MAX_RECORDS) snapshot.record_chain_truncated = true;

        const TypeField& context_field = require_field(exception_type, "context");
        PlatformNativeException context_fault;
        if (!platform_read_memory(static_cast<const std::byte*>(value.aggregate) + context_field.offset,
                                  &snapshot.context, sizeof(snapshot.context), &context_fault))
            throw NativeFault(context_fault);

        const TypeField& root_field = require_field(exception_type, "record");
        const TypeField& information_field = require_field(record_type, "ExceptionInformation");
        const TypeInfo& information_type = types_.get(information_field.type);
        const void* record = static_cast<const std::byte*>(value.aggregate) + root_field.offset;
        for (uint32_t i = 0; i < snapshot.record_count && record; ++i) {
            PlatformExceptionRecord& destination = snapshot.records[i];
            destination.ExceptionCode = static_cast<uint32_t>(read_scalar_field(record, record_type, "ExceptionCode"));
            destination.ExceptionFlags = static_cast<uint32_t>(read_scalar_field(record, record_type, "ExceptionFlags"));
            destination.ExceptionAddress = read_scalar_field(record, record_type, "ExceptionAddress");
            destination.NumberParameters = std::min<uint32_t>(
                static_cast<uint32_t>(read_scalar_field(record, record_type, "NumberParameters")), PLATFORM_EXCEPTION_MAX_PARAMETERS);
            PlatformNativeException info_fault;
            if (!platform_read_memory(static_cast<const std::byte*>(record) + information_field.offset,
                                      destination.ExceptionInformation,
                                      std::min<size_t>(information_type.size, sizeof(destination.ExceptionInformation)), &info_fault))
                throw NativeFault(info_fault);
            record = reinterpret_cast<const void*>(read_scalar_field(record, record_type, "ExceptionRecord"));
        }
        return snapshot;
    }

    Binding* resolve_binding(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (auto found = it->find(name); found != it->end()) return found->second;
        }
        return runtime_.find_global(name);
    }

    Binding* create_local(const std::string& name, TypeId type) {
        const TypeInfo& info = types_.get(type);
        const size_t size = types_.is_scalar(type) ? 8 : info.size;
        const size_t alignment = types_.is_scalar(type) ? 8 : info.alignment;
        auto binding = std::make_unique<Binding>();
        binding->type.store(type, std::memory_order_relaxed);
        binding->address = arena_.allocate(size, alignment);
        std::memset(binding->address, 0, size);
        Binding* raw = binding.get();
        bindings_.push_back(std::move(binding));
        scopes_.back()[name] = raw;
        return raw;
    }

    Binding* define_variable(const std::string& name, TypeId type) {
        if (Binding* existing = resolve_binding(name)) {
            const TypeId old = existing->type.load(std::memory_order_acquire);
            const bool scalar_retype = types_.is_scalar(old) && types_.is_scalar(type);
            if (!scalar_retype && old != type) throw std::runtime_error("cannot change variable storage type for " + name);
            existing->type.store(type, std::memory_order_release);
            return existing;
        }
        if (module_root_ && scopes_.size() == 1) return runtime_.define_or_redefine_global(name, type);
        return create_local(name, type);
    }

    static uint64_t mask_width(uint64_t bits, uint32_t size) {
        if (size >= 8) return bits;
        return bits & ((uint64_t{1} << (size * 8)) - 1);
    }

    uint64_t normalize_bits(uint64_t bits, TypeId type) const {
        const TypeInfo& info = types_.get(type);
        if (info.kind != RuntimeTypeKind::Primitive) return bits;
        switch (info.primitive) {
            case PrimitiveKind::I8: return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(bits)));
            case PrimitiveKind::I16: return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(bits)));
            case PrimitiveKind::I32: return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(bits)));
            case PrimitiveKind::U8: return static_cast<uint8_t>(bits);
            case PrimitiveKind::U16: return static_cast<uint16_t>(bits);
            case PrimitiveKind::U32: return static_cast<uint32_t>(bits);
            case PrimitiveKind::F32: return static_cast<uint32_t>(bits);
            default: return bits;
        }
    }

    RuntimeValue load_place(const Place& place) {
        const TypeInfo& info = types_.get(place.type);
        if (types_.is_aggregate(place.type)) return RuntimeValue{place.type, 0, place.address};
        uint64_t bits = 0;
        PlatformNativeException fault;
        if (!platform_read_memory(place.address, &bits, info.size, &fault)) throw NativeFault(fault);
        return RuntimeValue{place.type, normalize_bits(bits, place.type), nullptr};
    }

    void store_place(const Place& place, RuntimeValue value) {
        value = cast(value, place.type);
        const TypeInfo& info = types_.get(place.type);
        if (types_.is_aggregate(place.type)) {
            if (!value.aggregate) throw std::runtime_error("aggregate value has no storage");
            PlatformNativeException fault;
            if (!platform_write_memory(place.address, value.aggregate, info.size, &fault)) throw NativeFault(fault);
        } else {
            const uint64_t bits = mask_width(value.bits, info.size);
            PlatformNativeException fault;
            if (!platform_write_memory(place.address, &bits, info.size, &fault)) throw NativeFault(fault);
        }
    }

    void store_binding(Binding& binding, RuntimeValue value) {
        const TypeId target = binding.type.load(std::memory_order_acquire);
        if (target != value.type) throw std::runtime_error("binding store type mismatch");
        if (types_.is_scalar(target)) {
            std::memset(binding.address, 0, 8);
            const TypeInfo& info = types_.get(target);
            const uint64_t bits = mask_width(value.bits, info.size);
            std::memcpy(binding.address, &bits, info.size);
        } else {
            const TypeInfo& info = types_.get(target);
            if (!value.aggregate) throw std::runtime_error("aggregate value has no storage");
            std::memcpy(binding.address, value.aggregate, info.size);
        }
    }

    uint32_t evaluate_array_count(Expr& expr) {
        RuntimeValue value = eval(expr);
        const TypeInfo& info = types_.get(value.type);
        if (info.kind != RuntimeTypeKind::Primitive || !types_.is_integer(value.type))
            throw DiagnosticError("array size expression must be an integer", expr.span);

        bool positive = true;
        uint64_t count = 0;
        switch (info.primitive) {
            case PrimitiveKind::I8: {
                const int64_t n = static_cast<int8_t>(value.bits); positive = n > 0; count = static_cast<uint64_t>(n); break;
            }
            case PrimitiveKind::I16: {
                const int64_t n = static_cast<int16_t>(value.bits); positive = n > 0; count = static_cast<uint64_t>(n); break;
            }
            case PrimitiveKind::I32: {
                const int64_t n = static_cast<int32_t>(value.bits); positive = n > 0; count = static_cast<uint64_t>(n); break;
            }
            case PrimitiveKind::I64: {
                const int64_t n = static_cast<int64_t>(value.bits); positive = n > 0; count = static_cast<uint64_t>(n); break;
            }
            case PrimitiveKind::U8: count = static_cast<uint8_t>(value.bits); positive = count != 0; break;
            case PrimitiveKind::U16: count = static_cast<uint16_t>(value.bits); positive = count != 0; break;
            case PrimitiveKind::U32: count = static_cast<uint32_t>(value.bits); positive = count != 0; break;
            case PrimitiveKind::U64: count = value.bits; positive = count != 0; break;
            default: positive = false; break;
        }
        if (!positive || count > UINT32_MAX)
            throw DiagnosticError("array size must be between 1 and 2^32-1", expr.span);
        return static_cast<uint32_t>(count);
    }

    TypeId materialize_type(TypeSyntax& syntax) {
        if (syntax.kind != TypeSyntax::Kind::Array) return syntax.resolved;
        const TypeInfo& checked = types_.get(syntax.resolved);
        if (!checked.runtime_sized) return syntax.resolved;

        const TypeId element = materialize_type(*syntax.element_type);
        const uint32_t count = syntax.element_count_expr ? evaluate_array_count(*syntax.element_count_expr) : syntax.element_count;
        try {
            return types_.make_array(element, count);
        } catch (const std::exception& e) {
            throw DiagnosticError(e.what(), syntax.span);
        }
    }

    Place place(Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Variable: {
                Binding* binding = resolve_binding(expr.text);
                if (!binding) throw DiagnosticError("undefined variable '" + expr.text + "'", expr.span);
                return {binding->type.load(std::memory_order_acquire), binding->address, binding};
            }
            case ExprKind::Member: {
                Place base = place(*expr.lhs);
                const TypeInfo& info = types_.get(base.type);
                auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& f) { return f.name == expr.text; });
                if (it == info.fields.end()) throw DiagnosticError("unknown field '" + expr.text + "'", expr.span);
                return {it->type, static_cast<std::byte*>(base.address) + it->offset, nullptr};
            }
            case ExprKind::Index: {
                Place base = place(*expr.lhs);
                const TypeInfo& info = types_.get(base.type);
                if (info.kind != RuntimeTypeKind::Array) throw DiagnosticError("indexing requires an array", expr.span);
                RuntimeValue index = eval(*expr.rhs);
                const uint64_t i = cast(index, types_.primitive(PrimitiveKind::U64)).bits;
                return {info.element_type, static_cast<std::byte*>(base.address) + i * types_.get(info.element_type).size, nullptr};
            }
            case ExprKind::Deref: {
                RuntimeValue address = eval(*expr.lhs);
                return {materialize_type(*expr.type), reinterpret_cast<void*>(address.bits), nullptr};
            }
            default: break;
        }
        throw DiagnosticError("expression is not assignable", expr.span);
    }

    double as_double(RuntimeValue value) {
        const TypeInfo& t = types_.get(value.type);
        if (t.kind == RuntimeTypeKind::Primitive && t.primitive == PrimitiveKind::F64) return std::bit_cast<double>(value.bits);
        if (t.kind == RuntimeTypeKind::Primitive && t.primitive == PrimitiveKind::F32) return static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(value.bits)));
        return static_cast<double>(static_cast<int64_t>(value.bits));
    }

    RuntimeValue cast(RuntimeValue value, TypeId target) {
        if (value.type == target) return value;
        if (types_.is_aggregate(value.type) || types_.is_aggregate(target)) {
            if (value.type != target) throw std::runtime_error("invalid aggregate cast");
            return value;
        }
        const TypeInfo& target_info = types_.get(target);
        const TypeInfo& source_info_for_strings = types_.get(value.type);
        if (target_info.kind == RuntimeTypeKind::Primitive && target_info.primitive == PrimitiveKind::WStr &&
            source_info_for_strings.kind == RuntimeTypeKind::Primitive && source_info_for_strings.primitive == PrimitiveKind::Str) {
            const char* text = reinterpret_cast<const char*>(value.bits);
            return {target, reinterpret_cast<uint64_t>(runtime_.intern_wstring(text ? std::string(text) : std::string{})), nullptr};
        }
        if (target_info.kind != RuntimeTypeKind::Primitive) throw std::runtime_error("invalid cast target");
        if (target_info.primitive == PrimitiveKind::Void) return RuntimeValue{target, 0, nullptr};

        const TypeInfo& source_info = source_info_for_strings;
        const bool source_float = source_info.kind == RuntimeTypeKind::Primitive &&
            (source_info.primitive == PrimitiveKind::F32 || source_info.primitive == PrimitiveKind::F64);
        const double number = source_float ? as_double(value) : static_cast<double>(static_cast<int64_t>(value.bits));
        const int64_t integer = source_float ? static_cast<int64_t>(number) : static_cast<int64_t>(value.bits);
        uint64_t bits = 0;
        switch (target_info.primitive) {
            case PrimitiveKind::F32: bits = std::bit_cast<uint32_t>(static_cast<float>(number)); break;
            case PrimitiveKind::F64: bits = std::bit_cast<uint64_t>(static_cast<double>(number)); break;
            case PrimitiveKind::I8: bits = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(integer))); break;
            case PrimitiveKind::I16: bits = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(integer))); break;
            case PrimitiveKind::I32: bits = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(integer))); break;
            case PrimitiveKind::I64: bits = static_cast<uint64_t>(integer); break;
            case PrimitiveKind::U8: bits = static_cast<uint8_t>(integer); break;
            case PrimitiveKind::U16: bits = static_cast<uint16_t>(integer); break;
            case PrimitiveKind::U32: bits = static_cast<uint32_t>(integer); break;
            case PrimitiveKind::U64: case PrimitiveKind::Ptr: case PrimitiveKind::Str: case PrimitiveKind::WStr:
                bits = static_cast<uint64_t>(integer); break;
            case PrimitiveKind::Void: break;
        }
        return RuntimeValue{target, bits, nullptr};
    }

    bool truthy(RuntimeValue value) {
        if (types_.is_float(value.type)) return as_double(value) != 0.0;
        return value.bits != 0;
    }

    bool switch_equal(RuntimeValue subject, RuntimeValue candidate) {
        candidate = cast(candidate, subject.type);
        if (types_.is_float(subject.type)) return as_double(subject) == as_double(candidate);
        return subject.bits == candidate.bits;
    }

    RuntimeValue eval_binary(Expr& expr) {
        if (expr.op == TokenKind::AmpAmp) {
            RuntimeValue left = eval(*expr.lhs);
            if (!truthy(left)) return {types_.primitive(PrimitiveKind::I64), 0, nullptr};
            return {types_.primitive(PrimitiveKind::I64), truthy(eval(*expr.rhs)) ? 1u : 0u, nullptr};
        }
        if (expr.op == TokenKind::PipePipe) {
            RuntimeValue left = eval(*expr.lhs);
            if (truthy(left)) return {types_.primitive(PrimitiveKind::I64), 1, nullptr};
            return {types_.primitive(PrimitiveKind::I64), truthy(eval(*expr.rhs)) ? 1u : 0u, nullptr};
        }
        RuntimeValue a = eval(*expr.lhs), b = eval(*expr.rhs);
        const bool floating = types_.is_float(a.type) || types_.is_float(b.type);
        const TypeId i64 = types_.primitive(PrimitiveKind::I64);

        auto compare = [&](auto pred) -> RuntimeValue {
            bool result = floating ? pred(as_double(a), as_double(b)) : pred(static_cast<int64_t>(a.bits), static_cast<int64_t>(b.bits));
            return {i64, result ? 1u : 0u, nullptr};
        };
        switch (expr.op) {
            case TokenKind::EqualEqual: return compare([](auto x, auto y) { return x == y; });
            case TokenKind::BangEqual: return compare([](auto x, auto y) { return x != y; });
            case TokenKind::Less: return compare([](auto x, auto y) { return x < y; });
            case TokenKind::LessEqual: return compare([](auto x, auto y) { return x <= y; });
            case TokenKind::Greater: return compare([](auto x, auto y) { return x > y; });
            case TokenKind::GreaterEqual: return compare([](auto x, auto y) { return x >= y; });
            default: break;
        }

        if (floating) {
            const double x = as_double(a), y = as_double(b);
            double r = 0;
            switch (expr.op) {
                case TokenKind::Plus: r = x + y; break;
                case TokenKind::Minus: r = x - y; break;
                case TokenKind::Star: r = x * y; break;
                case TokenKind::Slash: r = y == 0.0 ? 0.0 : x / y; break;
                default: throw DiagnosticError("invalid floating-point operator", expr.span);
            }
            if (expr.checked_type == types_.primitive(PrimitiveKind::F64)) return {expr.checked_type, std::bit_cast<uint64_t>(r), nullptr};
            return {expr.checked_type, std::bit_cast<uint32_t>(static_cast<float>(r)), nullptr};
        }

        const uint64_t x = a.bits, y = b.bits;
        const int64_t sx = static_cast<int64_t>(x), sy = static_cast<int64_t>(y);
        uint64_t r = 0;
        switch (expr.op) {
            case TokenKind::Plus: r = x + y; break;
            case TokenKind::Minus: r = x - y; break;
            case TokenKind::Star: r = x * y; break;
            case TokenKind::Slash:
                if (!sy) r = 0;
                else if (sx == std::numeric_limits<int64_t>::min() && sy == -1) r = static_cast<uint64_t>(sx);
                else r = static_cast<uint64_t>(sx / sy);
                break;
            case TokenKind::Percent:
                if (!sy || (sx == std::numeric_limits<int64_t>::min() && sy == -1)) r = 0;
                else r = static_cast<uint64_t>(sx % sy);
                break;
            case TokenKind::Amp: r = x & y; break;
            case TokenKind::Pipe: r = x | y; break;
            case TokenKind::Caret: r = x ^ y; break;
            case TokenKind::ShiftLeft: r = x << (y & 63); break;
            case TokenKind::ShiftRight: r = x >> (y & 63); break;
            default: throw DiagnosticError("invalid integer operator", expr.span);
        }
        return {expr.checked_type, r, nullptr};
    }

    std::string c_string(RuntimeValue value) {
        RuntimeValue ptr = cast(value, types_.primitive(PrimitiveKind::Str));
        return ptr.bits ? std::string(reinterpret_cast<const char*>(ptr.bits)) : std::string{};
    }

    RuntimeValue intrinsic(Expr& expr, const std::vector<RuntimeValue>& args) {
        const TypeId void_type = types_.primitive(PrimitiveKind::Void);
        if (expr.target_name == "load") {
            runtime_.dlls().load(c_string(args[0]), true);
            return {void_type, 0, nullptr};
        }
        if (expr.target_name == "focus") { runtime_.dlls().focus(c_string(args[0])); return {void_type, 0, nullptr}; }
        if (expr.target_name == "free") { runtime_.dlls().free(c_string(args[0])); return {void_type, 0, nullptr}; }
        if (expr.target_name == "dlls") {
            if (!runtime_.quiet()) for (const auto& name : runtime_.dlls().list()) std::printf("%s\n", name.c_str());
            return {void_type, 0, nullptr};
        }
        if (expr.target_name == "address") {
            void* p = runtime_.dlls().address(c_string(args[0]), c_string(args[1]));
            return {types_.primitive(PrimitiveKind::Ptr), reinterpret_cast<uint64_t>(p), nullptr};
        }
        if (expr.target_name == "hex") {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(cast(args[0], types_.primitive(PrimitiveKind::Ptr)).bits);
            const uint64_t count = cast(args[1], types_.primitive(PrimitiveKind::U64)).bits;
            for (uint64_t i = 0; i < count; ++i) {
                uint8_t byte = 0;
                PlatformNativeException fault;
                if (!platform_read_memory(p + i, &byte, 1, &fault)) throw NativeFault(fault);
                if (!runtime_.quiet()) std::printf("%02X%s", byte, ((i + 1) % 16 == 0 || i + 1 == count) ? "\n" : " ");
            }
            return {void_type, 0, nullptr};
        }
        if (expr.target_name == "quit") {
            const int code = args.empty() ? 0 : static_cast<int>(static_cast<int32_t>(args[0].bits));
            platform_fatal_exit(code);
        }
        throw std::runtime_error("unknown intrinsic '" + expr.target_name + "'");
    }

    RuntimeValue eval_call(Expr& expr) {
        std::vector<RuntimeValue> args;
        std::vector<uint64_t> bits;
        args.reserve(expr.args.size()); bits.reserve(expr.args.size());
        for (size_t i = 0; i < expr.args.size(); ++i) {
            RuntimeValue value = cast(eval(*expr.args[i].value), expr.checked_arg_types[i]);
            args.push_back(value); bits.push_back(value.bits);
        }
        if (expr.checked_intrinsic_target) return intrinsic(expr, args);

        if (expr.checked_script_target) {
            auto slot = runtime_.function_slot(expr.target_name);
            if (!slot) throw DiagnosticError("script function is not installed: '" + expr.target_name + "'", expr.span);
            FunctionVersion* function = runtime_.function_version(*slot);
            if (!function) throw DiagnosticError("script function has no installed version: '" + expr.target_name + "'", expr.span);
            return runtime_.invoke(*function, args);
        }

        void* target = nullptr;
        if (expr.call_target == CallTargetKind::Variable) {
            Binding* binding = resolve_binding(expr.target_name);
            if (!binding) throw DiagnosticError("undefined function pointer variable '" + expr.target_name + "'", expr.span);
            target = reinterpret_cast<void*>(load_place({binding->type.load(std::memory_order_acquire), binding->address, binding}).bits);
        } else if (expr.call_target == CallTargetKind::External) {
            target = runtime_.dlls().resolve_external(expr.module_name, expr.target_name);
        } else {
            target = runtime_.dlls().resolve_focused(expr.target_name);
        }
        const uint64_t result = ffi_call_scalar(target, types_, expr.checked_return_type, expr.checked_arg_types, bits);
        return {expr.checked_return_type, result, nullptr};
    }

    RuntimeValue eval(Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Integer: return {expr.checked_type, expr.integer, nullptr};
            case ExprKind::Float: return {expr.checked_type, std::bit_cast<uint32_t>(static_cast<float>(expr.floating)), nullptr};
            case ExprKind::String: return {expr.checked_type, reinterpret_cast<uint64_t>(runtime_.intern_string(expr.text)), nullptr};
            case ExprKind::Variable: return load_place(place(expr));
            case ExprKind::FunctionAddress: {
                void* p = runtime_.function_address(expr.text);
                if (!p) throw DiagnosticError("script function has no trampoline: '" + expr.text + "'", expr.span);
                return {expr.checked_type, reinterpret_cast<uint64_t>(p), nullptr};
            }
            case ExprKind::Unary: {
                if (expr.op == TokenKind::Amp) return {expr.checked_type, reinterpret_cast<uint64_t>(place(*expr.lhs).address), nullptr};
                RuntimeValue value = eval(*expr.lhs);
                if (expr.op == TokenKind::Bang) return {expr.checked_type, truthy(value) ? 0u : 1u, nullptr};
                if (expr.op == TokenKind::Tilde) return {expr.checked_type, ~value.bits, nullptr};
                if (expr.op == TokenKind::Minus) {
                    if (types_.is_float(value.type)) {
                        if (value.type == types_.primitive(PrimitiveKind::F64)) return {value.type, std::bit_cast<uint64_t>(-std::bit_cast<double>(value.bits)), nullptr};
                        return {value.type, std::bit_cast<uint32_t>(-std::bit_cast<float>(static_cast<uint32_t>(value.bits))), nullptr};
                    }
                    return {value.type, static_cast<uint64_t>(-static_cast<int64_t>(value.bits)), nullptr};
                }
                break;
            }
            case ExprKind::Deref: return load_place(place(expr));
            case ExprKind::Binary: return eval_binary(expr);
            case ExprKind::Member:
            case ExprKind::Index: return load_place(place(expr));
            case ExprKind::Call: return eval_call(expr);
            case ExprKind::TypedValue: {
                if (expr.type->resolved == types_.primitive(PrimitiveKind::WStr) && expr.lhs->kind == ExprKind::String)
                    return {expr.type->resolved, reinterpret_cast<uint64_t>(runtime_.intern_wstring(expr.lhs->text)), nullptr};
                if (expr.type->resolved == types_.primitive(PrimitiveKind::Str) && expr.lhs->kind == ExprKind::String)
                    return {expr.type->resolved, reinterpret_cast<uint64_t>(runtime_.intern_string(expr.lhs->text)), nullptr};
                return cast(eval(*expr.lhs), expr.type->resolved);
            }
            case ExprKind::Aggregate: {
                const TypeId type = expr.type->resolved;
                const TypeInfo& info = types_.get(type);
                void* memory = arena_.allocate(info.size, info.alignment);
                std::memset(memory, 0, info.size);
                for (auto& init : expr.initializers) {
                    auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& f) { return f.name == init.name; });
                    if (it == info.fields.end()) throw DiagnosticError("unknown field '" + init.name + "'", init.span);
                    store_place({it->type, static_cast<std::byte*>(memory) + it->offset, nullptr}, eval(*init.value));
                }
                return {type, 0, memory};
            }
            case ExprKind::Array: {
                const TypeId type = materialize_type(*expr.type);
                const TypeInfo& info = types_.get(type);
                if (expr.elements.size() > info.element_count)
                    throw DiagnosticError("too many elements in " + types_.name(type) + " initializer", expr.span);
                void* memory = arena_.allocate(info.size, info.alignment);
                if (expr.array_has_initializer) std::memset(memory, 0, info.size);
                const TypeInfo& element = types_.get(info.element_type);
                for (size_t i = 0; i < expr.elements.size(); ++i) {
                    store_place({info.element_type, static_cast<std::byte*>(memory) + i * element.size, nullptr}, eval(*expr.elements[i]));
                }
                return {type, 0, memory};
            }
            case ExprKind::SizeOf: {
                TypeId type = expr.type ? materialize_type(*expr.type) : expr.lhs->checked_type;
                if (!expr.type && types_.get(type).runtime_sized) type = place(*expr.lhs).type;
                return {expr.checked_type, types_.get(type).size, nullptr};
            }
            case ExprKind::AlignOf: {
                const TypeId type = expr.type ? expr.type->resolved : expr.lhs->checked_type;
                return {expr.checked_type, types_.get(type).alignment, nullptr};
            }
            case ExprKind::OffsetOf:
                return {expr.checked_type, types_.field_offset(expr.type->resolved, expr.member_path), nullptr};
        }
        throw DiagnosticError("unhandled expression", expr.span);
    }

    void print_value(RuntimeValue value) {
        if (runtime_.quiet() || value.type == types_.primitive(PrimitiveKind::Void)) return;
        const TypeInfo& t = types_.get(value.type);
        if (t.kind != RuntimeTypeKind::Primitive) { std::printf("= %s @ %p\n", t.debug_name.c_str(), value.aggregate); return; }
        switch (t.primitive) {
            case PrimitiveKind::F32: std::printf("= %g\n", std::bit_cast<float>(static_cast<uint32_t>(value.bits))); break;
            case PrimitiveKind::F64: std::printf("= %g\n", std::bit_cast<double>(value.bits)); break;
            case PrimitiveKind::Str: std::printf("= \"%s\"\n", value.bits ? reinterpret_cast<const char*>(value.bits) : "NULL"); break;
            case PrimitiveKind::Ptr: std::printf("= 0x%llX\n", static_cast<unsigned long long>(value.bits)); break;
            case PrimitiveKind::Void: break;
            default: std::printf("= %lld\n", static_cast<long long>(value.bits)); break;
        }
    }

    Flow execute(Stmt& stmt) {
        switch (stmt.kind) {
            case StmtKind::StructDefinition:
            case StmtKind::UnionDefinition:
                runtime_.install_named_type(stmt.name, stmt.defined_type); return {};
            case StmtKind::FunctionDefinition:
                throw std::runtime_error("function definition reached nested executor without module context");
            case StmtKind::Assignment: {
                RuntimeValue value = eval(*stmt.value);
                if (stmt.target->kind == ExprKind::Variable) {
                    Binding* binding = define_variable(stmt.target->text, value.type);
                    store_binding(*binding, value);
                } else {
                    store_place(place(*stmt.target), value);
                }
                return {};
            }
            case StmtKind::Call: {
                RuntimeValue value = eval(*stmt.expr);
                if (echo_) print_value(value);
                return {};
            }
            case StmtKind::Block:
                return run_scoped(stmt.body);
            case StmtKind::If: {
                if (truthy(eval(*stmt.expr))) return run_scoped(stmt.body);
                if (stmt.else_branch) return execute(*stmt.else_branch);
                return {};
            }
            case StmtKind::Switch: {
                const RuntimeValue subject = eval(*stmt.expr);
                const SwitchArm* default_arm = nullptr;

                for (auto& arm : stmt.switch_arms) {
                    if (arm.is_default) {
                        default_arm = &arm;
                        continue;
                    }

                    bool matched = false;
                    for (auto& value : arm.values) {
                        if (switch_equal(subject, eval(*value))) {
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) continue;

                    return run_scoped(arm.body);
                }

                if (default_arm) return run_scoped(default_arm->body);
                return {};
            }
            case StmtKind::Try: {
                try {
                    return run_scoped(stmt.body);
                } catch (const NativeFault& fault) {
                    RuntimeValue exception = make_native_exception_value(fault.snapshot());
                    scopes_.push_back({});
                    try {
                        Binding* binding = create_local(stmt.catch_name, exception.type);
                        store_binding(*binding, exception);
                        Flow flow = run(stmt.catch_body);
                        scopes_.pop_back();
                        return flow;
                    } catch (...) {
                        scopes_.pop_back();
                        throw;
                    }
                }
            }
            case StmtKind::Throw: {
                RuntimeValue exception = eval(*stmt.expr);
                throw NativeFault(snapshot_from_native_exception_value(exception));
            }
            case StmtKind::While: {
                while (truthy(eval(*stmt.expr))) {
                    Flow flow = run_scoped(stmt.body);
                    if (flow.kind == FlowKind::Break) break;
                    if (flow.kind == FlowKind::Continue) continue;
                    if (flow.kind == FlowKind::Return) return flow;
                }
                return {};
            }
            case StmtKind::Break: return {FlowKind::Break, {}};
            case StmtKind::Continue: return {FlowKind::Continue, {}};
            case StmtKind::Return:
                if (!stmt.expr) return {FlowKind::Return, RuntimeValue{types_.primitive(PrimitiveKind::Void), 0, nullptr}};
                {
                    RuntimeValue value = eval(*stmt.expr);
                    if (function_return_ != INVALID_TYPE) value = cast(value, function_return_);
                    return {FlowKind::Return, value};
                }
            case StmtKind::Assert:
                if (!truthy(eval(*stmt.expr))) {
                    std::string message = "assertion failed";
                    if (!stmt.name.empty()) message += ": " + stmt.name;
                    throw DiagnosticError(message, stmt.span);
                }
                return {};
        }
        return {};
    }

public:
    // Module execution needs to install function definitions with their owning module.
    Flow execute_top_level(Stmt& stmt, const std::shared_ptr<Module>& module) {
        if (stmt.kind == StmtKind::FunctionDefinition) { runtime_.install_function(module, stmt); return {}; }
        return execute(stmt);
    }
};
}

void Interpreter::execute_module(const std::shared_ptr<Module>& module, bool echo_call_results) {
    ThreadInvocationScope thread(runtime_);
    runtime_.retain_module(module); // definitions executed before a later failure must keep their AST alive
    Executor executor(runtime_, true, echo_call_results);
    for (auto& stmt : module->statements) {
        Flow flow = executor.execute_top_level(*stmt, module);
        if (flow.kind != FlowKind::Normal) throw DiagnosticError("control flow escaped top-level execution", stmt->span);
    }
}

RuntimeValue Interpreter::invoke(FunctionVersion& function, const std::vector<RuntimeValue>& args) {
    ThreadInvocationScope thread(runtime_);
    if (args.size() != function.signature.parameters.size()) throw std::runtime_error("callback argument count mismatch");
    Executor executor(runtime_, false, false, function.signature.return_type);
    for (size_t i = 0; i < args.size(); ++i) executor.bind_parameter(function.declaration->parameters[i].name, function.signature.parameters[i], args[i]);
    Flow flow = executor.run(function.declaration->body);
    if (flow.kind == FlowKind::Return) return flow.value;
    if (function.signature.return_type != runtime_.types().primitive(PrimitiveKind::Void))
        throw DiagnosticError("non-void function '" + function.declaration->name + "' reached the end without returning", function.declaration->span);
    return {function.signature.return_type, 0, nullptr};
}
