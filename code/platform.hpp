#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

using PlatformLibrary = void*;

struct PlatformM128A {
    uint64_t Low = 0;
    int64_t High = 0;
};

struct PlatformXmmSaveArea32 {
    uint16_t ControlWord = 0;
    uint16_t StatusWord = 0;
    uint8_t TagWord = 0;
    uint8_t Reserved1 = 0;
    uint16_t ErrorOpcode = 0;
    uint32_t ErrorOffset = 0;
    uint16_t ErrorSelector = 0;
    uint16_t Reserved2 = 0;
    uint32_t DataOffset = 0;
    uint16_t DataSelector = 0;
    uint16_t Reserved3 = 0;
    uint32_t MxCsr = 0;
    uint32_t MxCsr_Mask = 0;
    PlatformM128A FloatRegisters[8]{};
    PlatformM128A XmmRegisters[16]{};
    uint8_t Reserved4[96]{};
};

struct PlatformContextX64 {
    uint64_t P1Home = 0, P2Home = 0, P3Home = 0, P4Home = 0, P5Home = 0, P6Home = 0;
    uint32_t ContextFlags = 0;
    uint32_t MxCsr = 0;
    uint16_t SegCs = 0, SegDs = 0, SegEs = 0, SegFs = 0, SegGs = 0, SegSs = 0;
    uint32_t EFlags = 0;
    uint64_t Dr0 = 0, Dr1 = 0, Dr2 = 0, Dr3 = 0, Dr6 = 0, Dr7 = 0;
    uint64_t Rax = 0, Rcx = 0, Rdx = 0, Rbx = 0, Rsp = 0, Rbp = 0, Rsi = 0, Rdi = 0;
    uint64_t R8 = 0, R9 = 0, R10 = 0, R11 = 0, R12 = 0, R13 = 0, R14 = 0, R15 = 0, Rip = 0;
    PlatformXmmSaveArea32 FltSave{};
    PlatformM128A VectorRegister[26]{};
    uint64_t VectorControl = 0;
    uint64_t DebugControl = 0;
    uint64_t LastBranchToRip = 0;
    uint64_t LastBranchFromRip = 0;
    uint64_t LastExceptionToRip = 0;
    uint64_t LastExceptionFromRip = 0;
};

constexpr uint32_t PLATFORM_EXCEPTION_MAX_PARAMETERS = 15;
constexpr uint32_t PLATFORM_EXCEPTION_MAX_RECORDS = 16;

struct PlatformExceptionRecord {
    uint32_t ExceptionCode = 0;
    uint32_t ExceptionFlags = 0;
    uint64_t ExceptionAddress = 0;
    uint32_t NumberParameters = 0;
    uint64_t ExceptionInformation[PLATFORM_EXCEPTION_MAX_PARAMETERS]{};
};

struct PlatformNativeException {
    PlatformExceptionRecord records[PLATFORM_EXCEPTION_MAX_RECORDS]{};
    uint32_t record_count = 0;
    bool record_chain_truncated = false;
    PlatformContextX64 context{};
    uint32_t thread_id = 0;
    bool stack_recovered = false;
};

class NativeFault final : public std::exception {
public:
    explicit NativeFault(PlatformNativeException snapshot) : snapshot_(std::move(snapshot)) {
        const uint32_t code = snapshot_.record_count ? snapshot_.records[0].ExceptionCode : 0;
        const unsigned long long address = snapshot_.record_count
            ? static_cast<unsigned long long>(snapshot_.records[0].ExceptionAddress) : 0;
        std::snprintf(message_, sizeof(message_), "native exception 0x%08X at 0x%llX", code, address);
    }

    const char* what() const noexcept override { return message_; }
    const PlatformNativeException& snapshot() const noexcept { return snapshot_; }

private:
    PlatformNativeException snapshot_{};
    char message_[96]{};
};

struct PlatformCallResult {
    uint64_t bits = 0;
    bool crashed = false;
    PlatformNativeException exception{};
};

PlatformLibrary platform_load_library(const std::string& path);
void platform_free_library(PlatformLibrary library);
void* platform_find_symbol(PlatformLibrary library, const std::string& name);
void* platform_find_ordinal(PlatformLibrary library, uint16_t ordinal);
std::wstring platform_widen(std::string_view text);
std::string platform_raw_command_line_after(size_t argument_count);
PlatformCallResult platform_call_dynamic(void* function, const uint64_t* args, int arg_count, uint32_t float_mask);
bool platform_read_memory(const void* address, void* out, size_t bytes, PlatformNativeException* out_exception = nullptr);
bool platform_write_memory(void* address, const void* data, size_t bytes, PlatformNativeException* out_exception = nullptr);
void platform_prepare_thread_exception_handling();
uint32_t platform_current_thread_id();
[[noreturn]] void platform_fatal_exit(int code);
