#include <windows.h>

// This is the entry point. It's called when the DLL is loaded or unloaded.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: // Initialize once here
            break;
        case DLL_PROCESS_DETACH: // Cleanup here
            break;
    }
    return TRUE;
}

// The __declspec(dllexport) tells the linker to put this in the export table.
extern __declspec(dllexport) int Add(int a, int b) {
    return a + b;
}

extern __declspec(dllexport) int Test() {
    return 21;
}

extern __declspec(dllexport) const char* Test2(int a) {
    if (a == 5)
        return "mi bombo";
    return "no";
}

extern __declspec(dllexport) void print(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

extern __declspec(dllexport) const char* sprint(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsprintf(buffer, format, args);
    va_end(args);
    return buffer;
}

extern __declspec(dllexport) const char* sprint2(const char* format, ...) {
    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);

    // 2. Dry run: Determine required length
    // Passing NULL and 0 returns the length that WOULD be written
    int len = vsnprintf(NULL, 0, format, args);

    char *buffer = NULL;
    if (len >= 0) {
        // 3. Allocate (len + 1 for the null terminator)
        buffer = malloc(len + 1);
        
        if (buffer != NULL) {
            // 4. Actual format using the copied args
            vsnprintf(buffer, len + 1, format, args_copy);
        }
    }

    // 5. Clean up both va_lists
    va_end(args_copy);
    va_end(args);

    // Note: The caller is now responsible for calling free() on this pointer
    return buffer;
}

extern __declspec(dllexport) void* gimmewindowclass(const char* class_name) {
    WNDCLASSA *wc = malloc(sizeof(WNDCLASSA));
    memset(wc, 0, sizeof(WNDCLASSA));
    wc->lpfnWndProc = DefWindowProcA;
    wc->hInstance = GetModuleHandleA(NULL);
    wc->lpszClassName = class_name;

    return (void*)wc;
}

extern __declspec(dllexport) void PumpEvents() {
    MSG msg;
    int count = 100;

    while (count < 100 && GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        count++;
    }
}
