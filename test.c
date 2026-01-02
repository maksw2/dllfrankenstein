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

extern __declspec(dllexport) LRESULT CALLBACK WindowProcessA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            // Post a quit message to the message queue
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            HPEN hPen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
            HGDIOBJ hOldPen = SelectObject(hdc, hPen);
            MoveToEx(hdc, 50, 20, NULL);
            LineTo(hdc, 20, 80);
            LineTo(hdc, 80, 80);
            LineTo(hdc, 50, 20);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }

    // Let Windows handle any messages we don't care about
    return DefWindowProcA(hWnd, msg, wp, lp);
}

extern __declspec(dllexport) LRESULT CALLBACK WindowProcessW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            // Post a quit message to the message queue
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            HPEN hPen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
            HGDIOBJ hOldPen = SelectObject(hdc, hPen);
            MoveToEx(hdc, 50, 20, NULL);
            LineTo(hdc, 20, 80);
            LineTo(hdc, 80, 80);
            LineTo(hdc, 50, 20);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }

    // Let Windows handle any messages we don't care about
    return DefWindowProcW(hWnd, msg, wp, lp);
}
