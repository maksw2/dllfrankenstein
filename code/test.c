#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reason; (void)reserved;
    return TRUE;
}

__declspec(dllexport) int Add(int a, int b) { return a + b; }
__declspec(dllexport) float Addf(float a, float b) { return a + b; }
__declspec(dllexport) uint64_t Add6(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    return a + b + c + d + e + f;
}

__declspec(dllexport) const char* sprint(const char* format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    const int len = vsnprintf(NULL, 0, format, args);
    char* buffer = NULL;
    if (len >= 0) {
        buffer = (char*)malloc((size_t)len + 1);
        if (buffer) vsnprintf(buffer, (size_t)len + 1, format, copy);
    }
    va_end(copy);
    va_end(args);
    return buffer;
}

/* --- Callback ABI tests -------------------------------------------------- */
typedef int (__cdecl *I32Binary)(int, int);
typedef float (__cdecl *F32Binary)(float, float);
typedef int (__cdecl *I32Unary)(int);

__declspec(dllexport) int CallI32Binary(I32Binary fn, int a, int b) { return fn(a, b); }
__declspec(dllexport) float CallF32Binary(F32Binary fn, float a, float b) { return fn(a, b); }

static I32Unary g_stored_callback = NULL;
__declspec(dllexport) void StoreI32Callback(I32Unary fn) { g_stored_callback = fn; }
__declspec(dllexport) int CallStoredI32Callback(int value) { return g_stored_callback ? g_stored_callback(value) : -1; }

struct AsyncCall {
    HANDLE thread;
    I32Unary callback;
    int value;
    int result;
};

static DWORD WINAPI async_thread_main(LPVOID raw) {
    struct AsyncCall* call = (struct AsyncCall*)raw;
    call->result = call->callback(call->value);
    return 0;
}

__declspec(dllexport) void* StartAsyncCallback(I32Unary callback, int value) {
    struct AsyncCall* call = (struct AsyncCall*)calloc(1, sizeof(*call));
    if (!call) return NULL;
    call->callback = callback;
    call->value = value;
    call->thread = CreateThread(NULL, 0, async_thread_main, call, 0, NULL);
    if (!call->thread) { free(call); return NULL; }
    return call;
}

__declspec(dllexport) int JoinAsyncCallback(void* handle) {
    struct AsyncCall* call = (struct AsyncCall*)handle;
    if (!call) return -1;
    WaitForSingleObject(call->thread, INFINITE);
    CloseHandle(call->thread);
    const int result = call->result;
    free(call);
    return result;
}

/* --- MSVC aggregate-layout fixtures ------------------------------------ */
struct NormalLayout {
    uint8_t a;
    uint32_t b;
    uint16_t c;
    void* p;
};

#pragma pack(push, 1)
struct PackedLayout {
    uint8_t a;
    uint32_t b;
    uint16_t c;
    void* p;
};
#pragma pack(pop)

union TestUnion {
    uint8_t bytes[13];
    uint64_t qword;
    void* pointer;
};

struct NestedLayout {
    uint32_t tag;
    struct NormalLayout inner;
    uint8_t tail[7];
};

#define EXPORT_LAYOUT(prefix, type) \
    __declspec(dllexport) uint64_t prefix##_size(void) { return sizeof(type); } \
    __declspec(dllexport) uint64_t prefix##_align(void) { return __alignof(type); }

EXPORT_LAYOUT(NormalLayout, struct NormalLayout)
EXPORT_LAYOUT(PackedLayout, struct PackedLayout)
EXPORT_LAYOUT(TestUnion, union TestUnion)
EXPORT_LAYOUT(NestedLayout, struct NestedLayout)

__declspec(dllexport) uint64_t NormalLayout_b_offset(void) { return offsetof(struct NormalLayout, b); }
__declspec(dllexport) uint64_t NormalLayout_p_offset(void) { return offsetof(struct NormalLayout, p); }
__declspec(dllexport) uint64_t PackedLayout_b_offset(void) { return offsetof(struct PackedLayout, b); }
__declspec(dllexport) uint64_t NestedLayout_inner_offset(void) { return offsetof(struct NestedLayout, inner); }
__declspec(dllexport) uint64_t NestedLayout_tail_offset(void) { return offsetof(struct NestedLayout, tail); }

#pragma warning(disable: 4717)
#pragma warning(disable: 6011)
__declspec(dllexport) void AccessViolation(void) { *(volatile int*)0 = 0; }
__declspec(dllexport) void StackOverflow(void) { StackOverflow(); }
__declspec(dllexport) void IllegalInstruction(void) { __ud2(); }
__declspec(dllexport) void PrivInstruction(void) { __halt(); }
