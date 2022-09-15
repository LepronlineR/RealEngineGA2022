#include <stdlib.h>
#include <stdio.h>
#include "wm.h"
#include "heap.h"
#include "debug.h"

int main(int argc, char** argv) {

	debug_install_exception_handler();
	debug_set_print_mask(k_print_error | k_print_warning);

	heap_t* heap = heap_create(2 * 1024 * 1024);

	wm_window_t* window = wm_create(heap);

	// Main loop
	while(wm_pump(window) == 0){
		int x, y;
		uint32_t mask = wm_get_mouse_mask(window);
		wm_get_mouse_move(window, &x, &y);
		debug_print_line(k_print_info, 
			"MOUSE mask=%x move=%dx%x\n", mask, x, y);
	}

	wm_destroy(window);
	return 0;
}