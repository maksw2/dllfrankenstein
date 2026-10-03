#pragma once
#include "checker.hpp"
#include "platform.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Interpreter;

struct RuntimeValue {
    TypeId type = INVALID_TYPE;
    uint64_t bits = 0;
    void* aggregate = nullptr;
};

struct Binding {
    std::atomic<TypeId> type{INVALID_TYPE};
    void* address = nullptr;
};

class FrameArena {
public:
    FrameArena() = default;
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    void* allocate(size_t size, size_t alignment);
private:
    struct Chunk {
        std::unique_ptr<std::byte[]> bytes;
        size_t capacity = 0;
        size_t used = 0;
    };
    std::vector<Chunk> chunks_;
};

class DllRegistry {
public:
    ~DllRegistry();
    PlatformLibrary load(const std::string& path, bool focus);
    void focus(const std::string& path);
    void free(const std::string& path);
    void* resolve_external(const std::string& path, const std::string& symbol);
    void* resolve_focused(const std::string& symbol);
    void* address(const std::string& path, const std::string& symbol);
    std::vector<std::string> list() const;
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PlatformLibrary> libraries_;
    std::string focused_;
    PlatformLibrary load_locked(const std::string& path);
    void* symbol_locked(PlatformLibrary library, const std::string& symbol);
};

struct FunctionVersion {
    FunctionSignature signature;
    std::shared_ptr<Module> module;
    Stmt* declaration = nullptr;
};

struct FunctionSlot {
    std::string name;
    void* trampoline = nullptr;
    std::atomic<FunctionVersion*> current{nullptr};
};

struct RawCallbackFrame {
    uint64_t gpr[4];
    uint64_t xmm[4];
    const uint64_t* stack_args;
};

class Runtime {
public:
    explicit Runtime(bool quiet = false);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    TypeRegistry& types() { return types_; }
    const TypeRegistry& types() const { return types_; }
    DllRegistry& dlls() { return dlls_; }
    bool quiet() const { return quiet_; }

    SemanticSnapshot semantic_snapshot() const;
    void install_named_type(const std::string& name, TypeId type);

    Binding* find_global(const std::string& name) const;
    Binding* define_or_redefine_global(const std::string& name, TypeId type);

    void install_function(const std::shared_ptr<Module>& module, Stmt& declaration);
    void* function_address(const std::string& name) const;
    FunctionVersion* function_version(uint32_t slot) const;
    std::optional<uint32_t> function_slot(const std::string& name) const;

    const char* intern_string(const std::string& value);
    const wchar_t* intern_wstring(const std::string& value);

    RuntimeValue invoke(FunctionVersion& function, const std::vector<RuntimeValue>& args);
    [[noreturn]] void fatal_callback(uint32_t slot, const std::string& message);

    void retain_module(std::shared_ptr<Module> module);

private:
    TypeRegistry types_;
    DllRegistry dlls_;
    bool quiet_ = false;

    mutable std::mutex metadata_mutex_;
    FrameArena global_arena_;
    std::vector<std::unique_ptr<Binding>> global_bindings_;
    std::unordered_map<std::string, Binding*> globals_;
    std::unordered_map<std::string, TypeId> named_types_;

    std::array<FunctionSlot, 256> function_slots_{};
    std::unordered_map<std::string, uint32_t> function_names_;
    uint32_t function_count_ = 0;
    std::vector<std::unique_ptr<FunctionVersion>> function_versions_;

    std::unordered_map<std::string, std::unique_ptr<char[]>> strings_;
    std::unordered_map<std::string, std::unique_ptr<wchar_t[]>> wstrings_;
    std::vector<std::shared_ptr<Module>> retained_modules_;
};

Runtime* active_runtime();
extern "C" uint64_t dispatch_script_callback(uint32_t slot, const RawCallbackFrame* frame, uint32_t* floating_return);
extern "C" void* script_trampoline_table[256];
