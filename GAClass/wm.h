#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct wm_window_t wm_window_t;

// Windows operations
wm_window_t* wm_create();
int wm_pump(wm_window_t* window);
void wm_destroy(wm_window_t* window);

// Input commands
uint32_t wm_get_mouse_mask(wm_window_t* window);
uint32_t wm_get_key_mask(wm_window_t* window);
void wm_get_mouse_move(wm_window_t* window, int* x, int* y);