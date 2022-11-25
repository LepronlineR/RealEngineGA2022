#pragma once

typedef struct scene_t scene_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of a scene.
scene_t* scene_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render);

// Destroy an instance of a scene.
void scene_destroy(scene_t* scene);

// Per-frame update for our scene.
void scene_update(scene_t* scene);