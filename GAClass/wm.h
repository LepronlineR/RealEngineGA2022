typedef void* wm_window_t;

wm_window_t* wm_create();
int wm_pump(wm_window_t* window);
void wm_destroy(wm_window_t* window);