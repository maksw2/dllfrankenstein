#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#include "platform.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>

extern "C" uint64_t call_dynamic_function(void* func_ptr, const uint64_t* args, int arg_count, uint32_t float_mask);

static_assert(sizeof(PlatformM128A) == sizeof(M128A));
static_assert(offsetof(PlatformM128A, Low) == offsetof(M128A, Low));
static_assert(offsetof(PlatformM128A, High) == offsetof(M128A, High));
static_assert(sizeof(PlatformXmmSaveArea32) == sizeof(XMM_SAVE_AREA32));
static_assert(sizeof(PlatformContextX64) == sizeof(CONTEXT));
static_assert(offsetof(PlatformContextX64, Rip) == offsetof(CONTEXT, Rip));
static_assert(offsetof(PlatformContextX64, FltSave) == offsetof(CONTEXT, FltSave));
static_assert(offsetof(PlatformContextX64, VectorRegister) == offsetof(CONTEXT, VectorRegister));
static_assert(offsetof(PlatformContextX64, LastExceptionFromRip) == offsetof(CONTEXT, LastExceptionFromRip));

namespace {
LONG capture_exception(PlatformNativeException* out, EXCEPTION_POINTERS* pointers) noexcept {
    if (!out) return EXCEPTION_EXECUTE_HANDLER;
    std::memset(out, 0, sizeof(*out));
    out->thread_id = GetCurrentThreadId();

    if (pointers && pointers->ContextRecord) {
        std::memcpy(&out->context, pointers->ContextRecord, sizeof(out->context));
    }

    const EXCEPTION_RECORD* record = pointers ? pointers->ExceptionRecord : nullptr;
    while (record && out->record_count < PLATFORM_EXCEPTION_MAX_RECORDS) {
        PlatformExceptionRecord& copy = out->records[out->record_count++];
        copy.ExceptionCode = static_cast<uint32_t>(record->ExceptionCode);
        copy.ExceptionFlags = static_cast<uint32_t>(record->ExceptionFlags);
        copy.ExceptionAddress = reinterpret_cast<uint64_t>(record->ExceptionAddress);
        copy.NumberParameters = std::min<uint32_t>(static_cast<uint32_t>(record->NumberParameters), PLATFORM_EXCEPTION_MAX_PARAMETERS);
        for (uint32_t i = 0; i < copy.NumberParameters; ++i)
            copy.ExceptionInformation[i] = static_cast<uint64_t>(record->ExceptionInformation[i]);
        record = record->ExceptionRecord;
    }
    out->record_chain_truncated = record != nullptr;
    return EXCEPTION_EXECUTE_HANDLER;
}

void recover_stack_overflow(PlatformNativeException& exception) noexcept {
    if (exception.record_count && exception.records[0].ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        exception.stack_recovered = _resetstkoflw() != 0;
}
}

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
    } __except (capture_exception(&result.exception, GetExceptionInformation())) {
        result.crashed = true;
        recover_stack_overflow(result.exception);
    }
    return result;
}

bool platform_read_memory(const void* address, void* out, size_t bytes, PlatformNativeException* out_exception) {
    PlatformNativeException exception;
    __try {
        std::memcpy(out, address, bytes);
        return true;
    } __except (capture_exception(&exception, GetExceptionInformation())) {
        recover_stack_overflow(exception);
        if (out_exception) *out_exception = exception;
        return false;
    }
}

bool platform_write_memory(void* address, const void* data, size_t bytes, PlatformNativeException* out_exception) {
    PlatformNativeException exception;
    __try {
        std::memcpy(address, data, bytes);
        return true;
    } __except (capture_exception(&exception, GetExceptionInformation())) {
        recover_stack_overflow(exception);
        if (out_exception) *out_exception = exception;
        return false;
    }
}

void platform_prepare_thread_exception_handling() {
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
}

uint32_t platform_current_thread_id() { return GetCurrentThreadId(); }
[[noreturn]] void platform_fatal_exit(int code) { ExitProcess(static_cast<UINT>(code)); }
