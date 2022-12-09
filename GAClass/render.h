#pragma once

#include <stdbool.h>

// High-level graphics rendering interface.

typedef struct render_t render_t;

typedef struct ecs_entity_ref_t ecs_entity_ref_t;
typedef struct gpu_mesh_info_t gpu_mesh_info_t;
typedef struct gpu_shader_info_t gpu_shader_info_t;
typedef struct gpu_uniform_buffer_info_t gpu_uniform_buffer_info_t;
typedef struct heap_t heap_t;
typedef struct wm_window_t wm_window_t;
typedef struct gpu_t gpu_t;

// Create a render system.
render_t* render_create(heap_t* heap, wm_window_t* window, bool render_create);

// Destroy a render system.
void render_destroy(render_t* render);

// Push a model onto a queue of items to be rendered.
void render_push_model(render_t* render, ecs_entity_ref_t* entity, gpu_mesh_info_t* mesh, gpu_shader_info_t* shader, gpu_uniform_buffer_info_t* uniform);

// Push a model image onto a queue of items to be rendered.
void render_push_model_image(render_t* render, ecs_entity_ref_t* entity, gpu_image_mesh_info_t* mesh, gpu_shader_info_t* shader, gpu_uniform_buffer_info_t* uniform);

// Push an end-of-frame marker on a queue of items to be rendered.
void render_push_done(render_t* render);

// Get the GPU from the renderer
gpu_t* render_get_gpu(render_t* render);