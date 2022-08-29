#include <stdlib.h>
#include <stdio.h>
#include "wm.h"

int main(int argc, char** argv) {
	wm_window_t* window = wm_create();

	while(wm_pump(window) == 0){
		printf("frame\n");
	}

	wm_destroy(window);
	return 0;
}