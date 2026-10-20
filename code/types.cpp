#include "types.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
uint32_t align_up(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) return value;
    const uint32_t mask = alignment - 1;
    return (value + mask) & ~mask;
}
}

TypeId TypeRegistry::add_primitive(PrimitiveKind kind, const char* name, uint32_t size, uint32_t alignment) {
    TypeInfo t;
    t.kind = RuntimeTypeKind::Primitive;
    t.primitive = kind;
    t.debug_name = name;
    t.size = size;
    t.alignment = alignment;
    std::scoped_lock lock(mutex_);
    const TypeId id = static_cast<TypeId>(types_.size());
    types_.push_back(std::make_unique<TypeInfo>(std::move(t)));
    primitive_ids_[static_cast<size_t>(kind)] = id;
    builtins_[name] = id;
    return id;
}

TypeRegistry::TypeRegistry() {
    add_primitive(PrimitiveKind::I8, "i8", 1, 1);
    add_primitive(PrimitiveKind::I16, "i16", 2, 2);
    add_primitive(PrimitiveKind::I32, "i32", 4, 4);
    add_primitive(PrimitiveKind::I64, "i64", 8, 8);
    add_primitive(PrimitiveKind::U8, "u8", 1, 1);
    add_primitive(PrimitiveKind::U16, "u16", 2, 2);
    add_primitive(PrimitiveKind::U32, "u32", 4, 4);
    add_primitive(PrimitiveKind::U64, "u64", 8, 8);
    add_primitive(PrimitiveKind::F32, "f32", 4, 4);
    add_primitive(PrimitiveKind::F64, "f64", 8, 8);
    add_primitive(PrimitiveKind::Str, "str", 8, 8);
    add_primitive(PrimitiveKind::WStr, "wstr", 8, 8);
    add_primitive(PrimitiveKind::Ptr, "ptr", 8, 8);
    add_primitive(PrimitiveKind::Void, "void", 0, 1);

}

TypeId TypeRegistry::primitive(PrimitiveKind kind) const { return primitive_ids_[static_cast<size_t>(kind)]; }

TypeId TypeRegistry::lookup_builtin(const std::string& name) const {
    auto it = builtins_.find(name);
    return it == builtins_.end() ? INVALID_TYPE : it->second;
}

const TypeInfo& TypeRegistry::get(TypeId id) const {
    std::scoped_lock lock(mutex_);
    if (id >= types_.size()) throw std::out_of_range("invalid TypeId");
    return *types_[id];
}

TypeId TypeRegistry::make_array(TypeId element, uint32_t count) {
    if (count == 0) throw std::runtime_error("array size must be between 1 and 2^32-1");
    const uint64_t key = (static_cast<uint64_t>(element) << 32) | count;
    {
        std::scoped_lock lock(mutex_);
        if (auto it = arrays_.find(key); it != arrays_.end()) return it->second;
    }
    const TypeInfo& e = get(element);
    if (!e.runtime_sized && e.size != 0 && count > std::numeric_limits<uint32_t>::max() / e.size)
        throw std::runtime_error("array type is too large");
    TypeInfo t;
    t.kind = RuntimeTypeKind::Array;
    t.debug_name = name(element) + "[" + std::to_string(count) + "]";
    t.element_type = element;
    t.element_count = count;
    t.runtime_sized = e.runtime_sized;
    t.size = e.runtime_sized ? 0 : e.size * count;
    t.alignment = e.alignment;
    std::scoped_lock lock(mutex_);
    if (auto it = arrays_.find(key); it != arrays_.end()) return it->second;
    const TypeId id = static_cast<TypeId>(types_.size());
    types_.push_back(std::make_unique<TypeInfo>(std::move(t)));
    arrays_[key] = id;
    return id;
}

TypeId TypeRegistry::make_runtime_array(TypeId element) {
    {
        std::scoped_lock lock(mutex_);
        if (auto it = runtime_arrays_.find(element); it != runtime_arrays_.end()) return it->second;
    }
    const TypeInfo& e = get(element);
    TypeInfo t;
    t.kind = RuntimeTypeKind::Array;
    t.debug_name = name(element) + "[*]";
    t.element_type = element;
    t.element_count = 0;
    t.runtime_sized = true;
    t.size = 0;
    t.alignment = e.alignment;
    std::scoped_lock lock(mutex_);
    if (auto it = runtime_arrays_.find(element); it != runtime_arrays_.end()) return it->second;
    const TypeId id = static_cast<TypeId>(types_.size());
    types_.push_back(std::make_unique<TypeInfo>(std::move(t)));
    runtime_arrays_[element] = id;
    return id;
}

TypeId TypeRegistry::make_aggregate(bool is_union, std::string debug_name, uint32_t pack,
                                    const std::vector<std::pair<std::string, TypeId>>& fields) {
    TypeInfo t;
    t.kind = is_union ? RuntimeTypeKind::Union : RuntimeTypeKind::Struct;
    t.debug_name = std::move(debug_name);
    t.pack = pack;
    uint32_t size = 0;
    uint32_t aggregate_align = 1;

    for (const auto& [field_name, field_type] : fields) {
        const TypeInfo& f = get(field_type);
        if (f.kind == RuntimeTypeKind::Primitive && f.primitive == PrimitiveKind::Void)
            throw std::runtime_error("void fields are not allowed");
        const uint32_t field_align = std::min(f.alignment, pack);
        aggregate_align = std::max(aggregate_align, field_align);
        uint32_t offset = 0;
        if (is_union) {
            offset = 0;
            size = std::max(size, f.size);
        } else {
            offset = align_up(size, field_align);
            if (f.size > std::numeric_limits<uint32_t>::max() - offset) throw std::runtime_error("aggregate type is too large");
            size = offset + f.size;
        }
        t.fields.push_back(TypeField{field_name, field_type, offset});
    }
    t.alignment = aggregate_align;
    t.size = align_up(size, aggregate_align);

    std::scoped_lock lock(mutex_);
    const TypeId id = static_cast<TypeId>(types_.size());
    types_.push_back(std::make_unique<TypeInfo>(std::move(t)));
    return id;
}

bool TypeRegistry::is_integer(TypeId id) const { return category(id) == TypeCategory::Integer; }
bool TypeRegistry::is_float(TypeId id) const { return category(id) == TypeCategory::Float; }
bool TypeRegistry::is_scalar(TypeId id) const {
    const auto c = category(id);
    return c == TypeCategory::Integer || c == TypeCategory::Float || c == TypeCategory::String || c == TypeCategory::Pointer;
}
bool TypeRegistry::is_aggregate(TypeId id) const { return category(id) == TypeCategory::Aggregate; }

TypeCategory TypeRegistry::category(TypeId id) const {
    const TypeInfo& t = get(id);
    if (t.kind != RuntimeTypeKind::Primitive) return TypeCategory::Aggregate;
    switch (t.primitive) {
        case PrimitiveKind::I8: case PrimitiveKind::I16: case PrimitiveKind::I32: case PrimitiveKind::I64:
        case PrimitiveKind::U8: case PrimitiveKind::U16: case PrimitiveKind::U32: case PrimitiveKind::U64:
            return TypeCategory::Integer;
        case PrimitiveKind::F32: case PrimitiveKind::F64: return TypeCategory::Float;
        case PrimitiveKind::Str: case PrimitiveKind::WStr: return TypeCategory::String;
        case PrimitiveKind::Ptr: return TypeCategory::Pointer;
        case PrimitiveKind::Void: return TypeCategory::Void;
    }
    return TypeCategory::Void;
}

std::string TypeRegistry::name(TypeId id) const { return get(id).debug_name; }

uint64_t TypeRegistry::field_offset(TypeId aggregate, const std::vector<std::string>& path, TypeId* final_type) const {
    TypeId current = aggregate;
    uint64_t offset = 0;
    for (const std::string& part : path) {
        const TypeInfo& info = get(current);
        if (info.kind != RuntimeTypeKind::Struct && info.kind != RuntimeTypeKind::Union)
            throw std::runtime_error("member path crosses a non-aggregate type");
        auto it = std::find_if(info.fields.begin(), info.fields.end(), [&](const TypeField& f) { return f.name == part; });
        if (it == info.fields.end()) throw std::runtime_error("unknown field '" + part + "'");
        offset += it->offset;
        current = it->type;
    }
    if (final_type) *final_type = current;
    return offset;
}

bool type_can_assign(const TypeRegistry& types, TypeId from, TypeId to) {
    if (from == to) return true;
    const auto a = types.category(from), b = types.category(to);
    if (a == TypeCategory::Aggregate || b == TypeCategory::Aggregate) return false;
    if (a == TypeCategory::Void || b == TypeCategory::Void) return false;
    return a == b;
}

bool abi_type_supported(const TypeRegistry& types, TypeId type, bool allow_void) {
    const TypeInfo& t = types.get(type);
    if (t.kind != RuntimeTypeKind::Primitive) return false;
    if (t.primitive == PrimitiveKind::Void) return allow_void;
    return true;
}
