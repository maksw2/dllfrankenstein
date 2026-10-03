#include "ffi.hpp"
#include "platform.hpp"
#include <stdexcept>
#include <cstdio>

uint64_t ffi_call_scalar(void* address, const TypeRegistry& types, TypeId return_type,
                         std::span<const TypeId> argument_types, std::span<const uint64_t> argument_bits) {
    if (!address) throw std::runtime_error("attempted to call a null function pointer");
    if (argument_types.size() != argument_bits.size()) throw std::runtime_error("internal ABI argument mismatch");
    uint32_t float_mask = 0;
    for (size_t i = 0; i < argument_types.size() && i < 4; ++i) {
        if (types.is_float(argument_types[i])) float_mask |= (1u << static_cast<uint32_t>(i));
    }
    if (types.is_float(return_type)) float_mask |= 0x80000000u;
    const PlatformCallResult result = platform_call_dynamic(address, argument_bits.data(), static_cast<int>(argument_bits.size()), float_mask);
    if (result.crashed) {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "native call crashed with exception 0x%08X", result.out_exception_code);
        throw std::runtime_error(buffer);
    }
    return result.bits;
}
