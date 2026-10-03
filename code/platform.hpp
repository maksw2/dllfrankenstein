#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using PlatformLibrary = void*;

struct PlatformCallResult {
    uint64_t bits = 0;
    bool crashed = false;
    uint32_t out_exception_code = 0;
};

PlatformLibrary platform_load_library(const std::string& path);
void platform_free_library(PlatformLibrary library);
void* platform_find_symbol(PlatformLibrary library, const std::string& name);
void* platform_find_ordinal(PlatformLibrary library, uint16_t ordinal);
std::wstring platform_widen(std::string_view text);
std::string platform_raw_command_line_after(size_t argument_count);
PlatformCallResult platform_call_dynamic(void* function, const uint64_t* args, int arg_count, uint32_t float_mask);
bool platform_read_memory(const void* address, void* out, size_t bytes, uint32_t* out_exception_code = nullptr);
bool platform_write_memory(void* address, const void* data, size_t bytes, uint32_t* out_exception_code = nullptr);
uint32_t platform_current_thread_id();
[[noreturn]] void platform_fatal_exit(int code);
