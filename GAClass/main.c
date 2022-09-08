#include <stdlib.h>
#include <stdio.h>
#include "wm.h"
#include "heap.h"

int main(int argc, char** argv) {
	

	heap_t* heap = heap_create(2 * 1024 * 1024);
	void* ptr = heap_alloc(heap, 4353, 32);
	heap_free(heap, ptr);
	heap_destroy(heap);

	wm_window_t* window = wm_create(heap);

	while(wm_pump(window) == 0){
		//printf("frame\n");
	}

	wm_destroy(window);
	return 0;
}