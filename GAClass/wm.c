#include "wm.h"
#include "debug.h"
#include "heap.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

enum {
    k_mouse_button_left = 1 << 0,
    k_mouse_button_right = 1 << 1,
    k_mouse_button_middle = 1 << 2
};

enum {
    k_key_up = 1 << 0,
    k_key_down = 1 << 1,
    k_key_left = 1 << 2,
    k_key_right = 1 << 3
};

typedef struct wm_window_t {
    HWND hwnd;
    heap_t* heap;
    int quit;
    int has_focus;
    uint32_t mouse_mask;
    uint32_t key_mask;
    int mouse_x;
    int mouse_y;
} wm_window_t;

const struct {
    int virtual_key;
    int ga_key;
} k_key_map[] = {
    { .virtual_key = VK_LEFT, .ga_key = k_key_left },
    { .virtual_key = VK_RIGHT, .ga_key = k_key_right },
    { .virtual_key = VK_UP, .ga_key = k_key_up },
    { .virtual_key = VK_DOWN, .ga_key = k_key_down }
};

static LRESULT CALLBACK _window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    wm_window_t* win = GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_KEYDOWN:
        for (int x = 0; x < _countof(k_key_map); x++) {
            if (k_key_map[x].virtual_key == wParam) {
                win->key_mask |= k_key_map[x].ga_key;
            }
        }
        break;
    case WM_KEYUP:
        for (int x = 0; x < _countof(k_key_map); x++) {
            if (k_key_map[x].virtual_key == wParam) {
                win->key_mask ^= k_key_map[x].ga_key;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        win->mouse_mask |= k_mouse_button_left;
        break;
    case WM_LBUTTONUP:
        win->mouse_mask ^= k_mouse_button_left;
        break;
    case WM_RBUTTONDOWN:
        win->mouse_mask |= k_mouse_button_right;
        break;
    case WM_RBUTTONUP:
        win->mouse_mask ^= k_mouse_button_right;
        break;
    case WM_MBUTTONDOWN:
        win->mouse_mask |= k_mouse_button_middle;
        break;
    case WM_MBUTTONUP:
        win->mouse_mask ^= k_mouse_button_middle;
        break;
    case WM_MOUSEMOVE:
        if (win->has_focus) {
            POINT old_cursor;
            GetCursorPos(&old_cursor);
            RECT window_rect;
            GetWindowRect(hwnd, &window_rect);
            SetCursorPos((window_rect.left + window_rect.right) / 2, (window_rect.top + window_rect.bottom) / 2);
            POINT new_cursor;
            GetCursorPos(&new_cursor);
            win->mouse_x = old_cursor.x - new_cursor.x;
            win->mouse_y = old_cursor.y - new_cursor.y;
        }
        break;
    case WM_ACTIVATEAPP:
        ShowCursor(!wParam);
        win->has_focus = wParam;
        break;    case WM_CLOSE:
        win->quit = 1;
        return win->quit;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

wm_window_t* wm_create(heap_t* heap) {

    WNDCLASS wc = {
        .lpfnWndProc = _window_proc,
        .hInstance = GetModuleHandle(NULL),
        .lpszClassName = L"Real Engine"
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
        debug_print_line(k_print_error,
            "HWND failed to correctly load\n");
        return 0;
    }

    wm_window_t* win = heap_alloc(heap, sizeof(wm_window_t), 8);
    win->has_focus = 0;
    win->hwnd = hwnd;
    win->key_mask = 0;
    win->mouse_mask = 0;
    win->quit = 0;
    win->heap = heap;

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) win);

    ShowWindow(hwnd, SW_SHOWNORMAL);

	return win;
}

int wm_pump(wm_window_t* window) {
    MSG msg = { NULL };
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
	return window->quit;
}

uint32_t wm_get_mouse_mask(wm_window_t* window) {
    return window->mouse_mask;
}

uint32_t wm_get_key_mask(wm_window_t* window) {
    return window->key_mask;
}

void wm_get_mouse_move(wm_window_t* window, int* x, int* y) {
    *x = window->mouse_x;
    *y = window->mouse_y;
}

void wm_destroy(wm_window_t* window) {
    DestroyWindow(window->hwnd);
    heap_free(window->heap, window);
}