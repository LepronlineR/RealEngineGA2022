#include <stdlib.h>
#include <stdio.h>
#include "wm.h"
#include "heap.h"
#include "debug.h" 
#include "thread.h"

// Homework includes
#include "hw1.h"
#include "hw2.h"

static int thread_function(void* data) {
	return 0;
}

int main(int argc, char** argv) {

	debug_install_exception_handler();
	debug_set_print_mask(k_print_error | k_print_warning);

	//homework1_test();
	homework2_test();

	heap_t* heap = heap_create(2 * 1024 * 1024);
	wm_window_t* window = wm_create(heap);

	// THIS IS THE MAIN LOOP!
	while (!wm_pump(window)){

		int x, y;
		wm_get_mouse_move(window, &x, &y);

		uint32_t mask = wm_get_mouse_mask(window);

		debug_print_line(k_print_info, "MOUSE mask=%x move=%dx%d\n", mask, x, y);
	}

	wm_destroy(window);
	heap_destroy(heap);

	return 0;
}