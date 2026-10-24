#include "runtime.hpp"
#include "interpreter.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>

namespace {
std::atomic<Runtime*> g_active_runtime{nullptr};

std::string canonical_dll_name(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}
}

void* FrameArena::allocate(size_t size, size_t alignment) {
    if (size == 0) size = 1;
    if (alignment == 0) alignment = 1;
    auto try_chunk = [&](Chunk& chunk) -> void* {
        const uintptr_t base = reinterpret_cast<uintptr_t>(chunk.bytes.get());
        const uintptr_t current = base + chunk.used;
        const uintptr_t aligned = (current + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        const size_t next = static_cast<size_t>(aligned - base) + size;
        if (next > chunk.capacity) return nullptr;
        chunk.used = next;
        return reinterpret_cast<void*>(aligned);
    };
    if (!chunks_.empty()) if (void* p = try_chunk(chunks_.back())) return p;
    const size_t capacity = std::max<size_t>(64 * 1024, size + alignment);
    Chunk chunk;
    // The arena itself does not initialize storage. Callers that require zeroed
    // memory do so explicitly; this lets `T[N]` model an uninitialized automatic
    // array while `T[N] {}` remains explicitly zero-initialized.
    chunk.bytes.reset(new std::byte[capacity]);
    chunk.capacity = capacity;
    chunks_.push_back(std::move(chunk));
    void* p = try_chunk(chunks_.back());
    if (!p) throw std::bad_alloc{};
    return p;
}

DllRegistry::~DllRegistry() {
    std::scoped_lock lock(mutex_);
    for (auto& [_, lib] : libraries_) platform_free_library(lib);
}

PlatformLibrary DllRegistry::load_locked(const std::string& path) {
    const std::string key = canonical_dll_name(path);
    if (auto it = libraries_.find(key); it != libraries_.end()) return it->second;
    PlatformLibrary library = platform_load_library(path);
    if (!library) throw std::runtime_error("failed to load DLL '" + path + "'");
    libraries_[key] = library;
    return library;
}

void* DllRegistry::symbol_locked(PlatformLibrary library, const std::string& symbol) {
    if (!symbol.empty() && symbol[0] == '#') {
        try {
            const unsigned long value = std::stoul(symbol.substr(1));
            if (value == 0 || value > 0xFFFF) return nullptr;
            return platform_find_ordinal(library, static_cast<uint16_t>(value));
        } catch (...) { return nullptr; }
    }
    return platform_find_symbol(library, symbol);
}

PlatformLibrary DllRegistry::load(const std::string& path, bool set_focus) {
    std::scoped_lock lock(mutex_);
    PlatformLibrary library = load_locked(path);
    if (set_focus) focused_ = canonical_dll_name(path);
    return library;
}

void DllRegistry::focus(const std::string& path) {
    std::scoped_lock lock(mutex_);
    load_locked(path);
    focused_ = canonical_dll_name(path);
}

void DllRegistry::free(const std::string& path) {
    std::scoped_lock lock(mutex_);
    const std::string key = canonical_dll_name(path);
    auto it = libraries_.find(key);
    if (it == libraries_.end()) throw std::runtime_error("DLL is not loaded: '" + path + "'");
    platform_free_library(it->second);
    libraries_.erase(it);
    if (focused_ == key) focused_.clear();
}

void* DllRegistry::resolve_external(const std::string& path, const std::string& symbol) {
    std::scoped_lock lock(mutex_);
    PlatformLibrary library = load_locked(path);
    void* address = symbol_locked(library, symbol);
    if (!address) throw std::runtime_error("symbol '" + symbol + "' not found in '" + path + "'");
    return address;
}

void* DllRegistry::resolve_focused(const std::string& symbol) {
    std::scoped_lock lock(mutex_);
    if (focused_.empty()) throw std::runtime_error("no focused DLL for symbol '" + symbol + "'");
    auto it = libraries_.find(focused_);
    if (it == libraries_.end()) throw std::runtime_error("focused DLL is no longer loaded");
    void* address = symbol_locked(it->second, symbol);
    if (!address) throw std::runtime_error("symbol '" + symbol + "' not found in focused DLL");
    return address;
}

void* DllRegistry::address(const std::string& path, const std::string& symbol) {
    return resolve_external(path, symbol);
}

std::vector<std::string> DllRegistry::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(libraries_.size());
    for (const auto& [name, _] : libraries_) names.push_back(name + (name == focused_ ? " *" : ""));
    std::sort(names.begin(), names.end());
    return names;
}

Runtime::Runtime(bool quiet) : quiet_(quiet) {
    Runtime* expected = nullptr;
    if (!g_active_runtime.compare_exchange_strong(expected, this)) throw std::runtime_error("only one Runtime may be active in a process");
}

Runtime::~Runtime() {
    Runtime* expected = this;
    g_active_runtime.compare_exchange_strong(expected, nullptr);
}

Runtime* active_runtime() { return g_active_runtime.load(std::memory_order_acquire); }

SemanticSnapshot Runtime::semantic_snapshot() const {
    std::scoped_lock lock(metadata_mutex_);
    SemanticSnapshot snapshot;
    snapshot.named_types = named_types_;
    snapshot.ffi_functions = ffi_functions_;
    for (const auto& [name, binding] : globals_) snapshot.variables[name] = binding->type.load(std::memory_order_acquire);
    for (const auto& [name, slot_index] : function_names_) {
        if (FunctionVersion* version = function_slots_[slot_index].current.load(std::memory_order_acquire)) snapshot.functions[name] = version->signature;
    }
    return snapshot;
}

void Runtime::install_ffi_bindings(const std::unordered_map<std::string, FfiBindingSignature>& bindings) {
    std::scoped_lock lock(metadata_mutex_);
    ffi_functions_ = bindings;
}

void Runtime::install_named_type(const std::string& name, TypeId type) {
    std::scoped_lock lock(metadata_mutex_);
    named_types_[name] = type;
}

Binding* Runtime::find_global(const std::string& name) const {
    std::scoped_lock lock(metadata_mutex_);
    auto it = globals_.find(name);
    return it == globals_.end() ? nullptr : it->second;
}

Binding* Runtime::define_or_redefine_global(const std::string& name, TypeId type) {
    std::scoped_lock lock(metadata_mutex_);
    if (auto it = globals_.find(name); it != globals_.end()) {
        Binding* binding = it->second;
        const TypeId old = binding->type.load(std::memory_order_acquire);
        const bool scalar_retype = types_.is_scalar(old) && types_.is_scalar(type);
        if (!scalar_retype && old != type) throw std::runtime_error("cannot change global variable storage type for " + name);
        binding->type.store(type, std::memory_order_release);
        return binding;
    }
    const TypeInfo& info = types_.get(type);
    const size_t size = types_.is_scalar(type) ? 8 : info.size;
    const size_t alignment = types_.is_scalar(type) ? 8 : info.alignment;
    void* address = global_arena_.allocate(size, alignment);
    std::memset(address, 0, size);
    auto binding = std::make_unique<Binding>();
    binding->type.store(type, std::memory_order_relaxed);
    binding->address = address;
    Binding* raw = binding.get();
    global_bindings_.push_back(std::move(binding));
    globals_[name] = raw;
    return raw;
}

void* Runtime::allocate_persistent(size_t size, size_t alignment) {
    std::scoped_lock lock(metadata_mutex_);
    return global_arena_.allocate(size, alignment);
}

void Runtime::install_function(const std::shared_ptr<Module>& module, Stmt& declaration) {
    auto version = std::make_unique<FunctionVersion>();
    version->signature.return_type = declaration.checked_return_type;
    version->signature.span = declaration.span;
    for (const auto& p : declaration.parameters) version->signature.parameters.push_back(p.checked_type);
    version->module = module;
    version->declaration = &declaration;
    FunctionVersion* raw = version.get();

    std::scoped_lock lock(metadata_mutex_);
    uint32_t slot_index;
    if (auto it = function_names_.find(declaration.name); it != function_names_.end()) {
        slot_index = it->second;
    } else {
        if (function_count_ >= function_slots_.size()) throw std::runtime_error("script function trampoline limit exceeded (256 distinct names)");
        slot_index = function_count_++;
        function_names_[declaration.name] = slot_index;
        function_slots_[slot_index].name = declaration.name;
        function_slots_[slot_index].trampoline = script_trampoline_table[slot_index];
    }
    function_versions_.push_back(std::move(version));
    function_slots_[slot_index].current.store(raw, std::memory_order_release);
}

std::optional<uint32_t> Runtime::function_slot(const std::string& name) const {
    std::scoped_lock lock(metadata_mutex_);
    auto it = function_names_.find(name);
    if (it == function_names_.end()) return std::nullopt;
    return it->second;
}

void* Runtime::function_address(const std::string& name) const {
    std::scoped_lock lock(metadata_mutex_);
    auto it = function_names_.find(name);
    if (it == function_names_.end()) return nullptr;
    return function_slots_[it->second].trampoline;
}

FunctionVersion* Runtime::function_version(uint32_t slot) const {
    if (slot >= function_slots_.size()) return nullptr;
    return function_slots_[slot].current.load(std::memory_order_acquire);
}

const char* Runtime::intern_string(const std::string& value) {
    std::scoped_lock lock(metadata_mutex_);
    if (auto it = strings_.find(value); it != strings_.end()) return it->second.get();
    auto data = std::make_unique<char[]>(value.size() + 1);
    if (!value.empty()) std::memcpy(data.get(), value.data(), value.size());
    data[value.size()] = '\0';
    const char* out = data.get();
    strings_[value] = std::move(data);
    return out;
}

const wchar_t* Runtime::intern_wstring(const std::string& value) {
    std::scoped_lock lock(metadata_mutex_);
    if (auto it = wstrings_.find(value); it != wstrings_.end()) return it->second.get();
    const std::wstring wide = platform_widen(value);
    auto data = std::make_unique<wchar_t[]>(wide.size() + 1);
    if (!wide.empty()) std::memcpy(data.get(), wide.data(), wide.size() * sizeof(wchar_t));
    data[wide.size()] = L'\0';
    const wchar_t* out = data.get();
    wstrings_[value] = std::move(data);
    return out;
}

RuntimeValue Runtime::invoke(FunctionVersion& function, const std::vector<RuntimeValue>& args) {
    Interpreter interpreter(*this);
    return interpreter.invoke(function, args);
}

[[noreturn]] void Runtime::fatal_callback(uint32_t slot, const std::string& message) {
    const char* name = "<invalid slot>";
    if (slot < function_slots_.size() && !function_slots_[slot].name.empty()) name = function_slots_[slot].name.c_str();
    std::fprintf(stderr, "\nfatal: interpreter error in native callback\n  function: %s\n  trampoline: %u\n  thread: %u\n  %s\n",
                 name, slot, platform_current_thread_id(), message.c_str());
    std::fflush(stderr);
    platform_fatal_exit(70);
}

void Runtime::retain_module(std::shared_ptr<Module> module) {
    std::scoped_lock lock(metadata_mutex_);
    retained_modules_.push_back(std::move(module));
}

extern "C" uint64_t dispatch_script_callback(uint32_t slot, const RawCallbackFrame* frame, uint32_t* floating_return) {
    Runtime* runtime = active_runtime();
    if (!runtime) platform_fatal_exit(71);
    FunctionVersion* function = runtime->function_version(slot);
    if (!function) runtime->fatal_callback(slot, "trampoline has no installed function");
    try {
        std::vector<RuntimeValue> args;
        args.reserve(function->signature.parameters.size());
        for (size_t i = 0; i < function->signature.parameters.size(); ++i) {
            const TypeId type = function->signature.parameters[i];
            uint64_t bits;
            if (i < 4) bits = runtime->types().is_float(type) ? frame->xmm[i] : frame->gpr[i];
            else bits = frame->stack_args[i - 4];
            args.push_back(RuntimeValue{type, bits, nullptr});
        }
        RuntimeValue result = runtime->invoke(*function, args);
        *floating_return = runtime->types().is_float(function->signature.return_type) ? 1u : 0u;
        return result.bits;
    } catch (const DiagnosticError& e) {
        runtime->fatal_callback(slot, std::string(e.what()) + " (line " + std::to_string(e.span.line) + ")");
    } catch (const std::exception& e) {
        runtime->fatal_callback(slot, e.what());
    } catch (...) {
        runtime->fatal_callback(slot, "unknown interpreter failure");
    }
}
