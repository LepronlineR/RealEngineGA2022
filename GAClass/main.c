#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "render.h"
#include "timer.h"
#include "wm.h"
#include "scene.h"

#include <GLFW/glfw3.h>

int bain(int argc, const char* argv[])
{
	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);
	debug_install_exception_handler();

	timer_startup();

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	render_t* render = render_create(heap, window, true);

	scene_t* scene = scene_create(heap, fs, window, render);

	while (!wm_pump(window)) {
		scene_update(scene);
	}

	render_destroy(render);

	scene_destroy(scene);

	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}