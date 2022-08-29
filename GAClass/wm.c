#include <stddef.h>
#include "wm.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int s_quit = 0;

static LRESULT CALLBACK _window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CLOSE:
        s_quit = 1;
        return s_quit;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

wm_window_t* wm_create() {

    WNDCLASS wc = {
        .lpfnWndProc = _window_proc,
        .hInstance = GetModuleHandle(NULL),
        .lpszClassName = L"GA2022 Window Class"
    };

	RegisterClass(&wc);

    // Create the window.
    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        wc.lpszClassName,               // Window class
        L"dn",                          // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        NULL,           // Parent window    
        NULL,           // Menu
        wc.hInstance,   // Instance handle
        NULL            // Additional application data
    );

    if (hwnd == NULL){
        return 0;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);

	return (wm_window_t*) hwnd;
}

int wm_pump(wm_window_t* window) {
    MSG msg = { NULL };
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
	return s_quit;
}


void wm_destroy(wm_window_t* window) {
    DestroyWindow(window);
}