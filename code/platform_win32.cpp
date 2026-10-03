#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform.hpp"
#include <cstring>

extern "C" uint64_t call_dynamic_function(void* func_ptr, const uint64_t* args, int arg_count, uint32_t float_mask);

PlatformLibrary platform_load_library(const std::string& path) {
    return reinterpret_cast<PlatformLibrary>(LoadLibraryA(path.c_str()));
}

void platform_free_library(PlatformLibrary library) {
    if (library) FreeLibrary(reinterpret_cast<HMODULE>(library));
}

void* platform_find_symbol(PlatformLibrary library, const std::string& name) {
    if (!library) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name.c_str()));
}

void* platform_find_ordinal(PlatformLibrary library, uint16_t ordinal) {
    if (!library) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), MAKEINTRESOURCEA(ordinal)));
}

std::wstring platform_widen(std::string_view text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring out(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), required);
    return out;
}

std::string platform_raw_command_line_after(size_t argument_count) {
    const char* cursor = GetCommandLineA();
    if (!cursor) return {};

    for (size_t argument = 0; argument < argument_count; ++argument) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        bool quoted = false;
        while (*cursor) {
            if (*cursor == '"') {
                quoted = !quoted;
                ++cursor;
                continue;
            }
            if (!quoted && (*cursor == ' ' || *cursor == '\t')) break;
            ++cursor;
        }
    }

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    return cursor;
}

PlatformCallResult platform_call_dynamic(void* function, const uint64_t* args, int arg_count, uint32_t float_mask) {
    PlatformCallResult result;
    __try {
        result.bits = call_dynamic_function(function, args, arg_count, float_mask);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.crashed = true;
        result.out_exception_code = static_cast<uint32_t>(GetExceptionCode());
    }
    return result;
}

bool platform_read_memory(const void* address, void* out, size_t bytes, uint32_t* out_exception_code) {
    __try {
        std::memcpy(out, address, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_exception_code) *out_exception_code = static_cast<uint32_t>(GetExceptionCode());
        return false;
    }
}

bool platform_write_memory(void* address, const void* data, size_t bytes, uint32_t* out_exception_code) {
    __try {
        std::memcpy(address, data, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_exception_code) *out_exception_code = static_cast<uint32_t>(GetExceptionCode());
        return false;
    }
}

uint32_t platform_current_thread_id() { return GetCurrentThreadId(); }
[[noreturn]] void platform_fatal_exit(int code) { ExitProcess(static_cast<UINT>(code)); }
