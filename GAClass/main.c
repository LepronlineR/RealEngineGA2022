#include "atomic.h"
#include "debug.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "mat4f.h"
#include "mutex.h"
#include "quatf.h"
#include "thread.h"
#include "timer.h"
#include "timer_object.h"
#include "trace.h"
#include "vec3f.h"
#include "wm.h"

#include <assert.h>
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <windows.h>

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
	// homework3_test();
	
	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	gpu_t* gpu = gpu_create(heap, window);
	timer_object_t* root_time = timer_object_create(heap, NULL);

	// Setup for rendering a cube!
	fs_work_t* vertex_shader_work = fs_read(fs, "shaders/triangle.vert.spv", heap, false, false);
	fs_work_t* fragment_shader_work = fs_read(fs, "shaders/triangle.frag.spv", heap, false, false);
	gpu_shader_info_t shader_info =
	{
		.vertex_shader_data = fs_work_get_buffer(vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	gpu_shader_t* shader = gpu_shader_create(gpu, &shader_info);
	fs_work_destroy(fragment_shader_work);
	fs_work_destroy(vertex_shader_work);

	gpu_pipeline_info_t pipeline_info =
	{
		.shader = shader,
		.mesh_layout = k_gpu_mesh_layout_tri_p444_c444_i2,
	};
	gpu_pipeline_t* pipeline = gpu_pipeline_create(gpu, &pipeline_info);

	vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  1.0f },
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	gpu_mesh_info_t mesh_info =
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};

	gpu_mesh_t* mesh = gpu_mesh_create(gpu, &mesh_info);

	struct
	{
		mat4f_t projection;
		mat4f_t model;
		mat4f_t view;
	} uniform_data;
	mat4f_make_perspective(&uniform_data.projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&uniform_data.view, &eye_pos, &forward, &up);

	mat4f_make_identity(&uniform_data.model);

	gpu_uniform_buffer_info_t uniform_info =
	{
		.data = &uniform_data,
		.size = sizeof(uniform_data),
	};
	gpu_uniform_buffer_t* uniform_buffer = gpu_uniform_buffer_create(gpu, &uniform_info);

	gpu_descriptor_info_t descriptor_info =
	{
		.shader = shader,
		.uniform_buffers = &uniform_buffer,
		.uniform_buffer_count = 1,
	};
	gpu_descriptor_t* descriptor = gpu_descriptor_create(gpu, &descriptor_info);

	vec3f_t angles = vec3f_zero();

	// THIS IS THE MAIN LOOP!
	while (!wm_pump(window)){
		timer_object_update(root_time);

		int x, y;
		wm_get_mouse_move(window, &x, &y);

		uint32_t mask = wm_get_mouse_mask(window);

		uint32_t now = timer_ticks_to_ms(timer_get_ticks());
		debug_print_line(
			k_print_info,
			"T=%dms, MOUSE mask=%x move=%dx%d\n",
			timer_object_get_ms(root_time),
			mask,
			x, y);

		gpu_cmd_buffer_t* cmd_buf = gpu_frame_begin(gpu);

		angles.y += 0.01f;
		quatf_t orient = quatf_from_eulers(angles);
		mat4f_make_rotation(&uniform_data.model, &orient);
		gpu_uniform_buffer_update(gpu, uniform_buffer, &uniform_data, sizeof(uniform_data));

		gpu_cmd_pipeline_bind(gpu, cmd_buf, pipeline);
		gpu_cmd_mesh_bind(gpu, cmd_buf, mesh);
		gpu_cmd_descriptor_bind(gpu, cmd_buf, descriptor);
		gpu_cmd_draw(gpu, cmd_buf);
		gpu_frame_end(gpu);
	}

	gpu_descriptor_destroy(gpu, descriptor);
	gpu_uniform_buffer_destroy(gpu, uniform_buffer);
	gpu_mesh_destroy(gpu, mesh);
	gpu_pipeline_destroy(gpu, pipeline);
	gpu_shader_destroy(gpu, shader);

	timer_object_destroy(root_time);
	gpu_destroy(gpu);
	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}