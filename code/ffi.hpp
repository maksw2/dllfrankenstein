#pragma once
#include "types.hpp"
#include <cstdint>
#include <span>

uint64_t ffi_call_scalar(void* address, const TypeRegistry& types, TypeId return_type,
                         std::span<const TypeId> argument_types, std::span<const uint64_t> argument_bits);
