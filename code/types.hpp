#pragma once
#include "ast.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

enum class PrimitiveKind {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Str, WStr,
    Ptr,
    Void,
};

enum class RuntimeTypeKind { Primitive, Struct, Union, Array };
enum class TypeCategory { Integer, Float, String, Pointer, Aggregate, Void };

struct TypeField {
    std::string name;
    TypeId type = INVALID_TYPE;
    uint32_t offset = 0;
};

struct TypeInfo {
    RuntimeTypeKind kind = RuntimeTypeKind::Primitive;
    PrimitiveKind primitive = PrimitiveKind::Void;
    std::string debug_name;
    uint32_t size = 0;
    uint32_t alignment = 1;
    uint32_t pack = 8;
    TypeId element_type = INVALID_TYPE;
    uint32_t element_count = 0;
    std::vector<TypeField> fields;
};

class TypeRegistry {
public:
    TypeRegistry();

    TypeId primitive(PrimitiveKind kind) const;
    TypeId lookup_builtin(const std::string& name) const;
    const TypeInfo& get(TypeId id) const;

    TypeId make_array(TypeId element, uint32_t count);
    TypeId make_aggregate(bool is_union, std::string debug_name, uint32_t pack,
                          const std::vector<std::pair<std::string, TypeId>>& fields);

    bool is_integer(TypeId id) const;
    bool is_float(TypeId id) const;
    bool is_scalar(TypeId id) const;
    bool is_aggregate(TypeId id) const;
    TypeCategory category(TypeId id) const;
    std::string name(TypeId id) const;

    uint64_t field_offset(TypeId aggregate, const std::vector<std::string>& path, TypeId* final_type = nullptr) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<TypeInfo>> types_;
    std::unordered_map<std::string, TypeId> builtins_;
    std::unordered_map<uint64_t, TypeId> arrays_;
    TypeId primitive_ids_[14]{};

    TypeId add_primitive(PrimitiveKind kind, const char* name, uint32_t size, uint32_t alignment);
};

bool type_can_assign(const TypeRegistry& types, TypeId from, TypeId to);
bool abi_type_supported(const TypeRegistry& types, TypeId type, bool allow_void);
