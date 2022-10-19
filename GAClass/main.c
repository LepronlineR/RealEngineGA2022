#include <stdlib.h>
#include <stdio.h>
#include "wm.h"
#include "heap.h"
#include "debug.h" 
#include "thread.h"
#include "timer.h"
#include "timer_object.h"
#include "trace.h"

// Homework includes
#include "hw1.h"
#include "hw2.h"
#include "hw3.h"

static int thread_function(void* data) {
	return 0;
}

int main(int argc, char** argv) {

	debug_install_exception_handler();
	timer_startup();
	debug_set_print_mask(k_print_error | k_print_warning);

	// homework1_test();
	// homework2_test();
	homework3_test();

	
	heap_t* heap = heap_create(2 * 1024 * 1024);
	wm_window_t* window = wm_create(heap);
	timer_object_t* root_time = timer_object_create(heap, NULL);

	// THIS IS THE MAIN LOOP!
	while (!wm_pump(window)){
		timer_object_update(root_time);

		int x, y;
		wm_get_mouse_move(window, &x, &y);

		uint32_t mask = wm_get_mouse_mask(window);

		uint32_t now = timer_ticks_to_ms(timer_get_ticks());
		debug_print_line(k_print_info, "T=%dms, MOUSE mask=%x move=%dx%d\n",
			timer_object_get_ms(root_time), mask, x, y);
	}

	timer_object_destroy(root_time);
	wm_destroy(window);
	heap_destroy(heap);
	

	return 0;
}