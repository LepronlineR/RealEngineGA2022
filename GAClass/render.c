#include "render.h"

#include "ecs.h"
#include "gpu.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "wm.h"
#include "debug.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "imgui_impl_vulkan.h"
#include "imgui_impl_win32.h"

#define IMGUI_VERSION               "1.89.1 WIP"
#define IMGUI_CHECKVERSION()        igDebugCheckVersionAndDataLayout(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx))
#define IM_ARRAYSIZE(_ARR)          ((int)(sizeof(_ARR) / sizeof(*_ARR)))  

enum
{
	k_render_max_drawables = 512,
};

enum
{
	k_texture_sampler_binding = 1
};

static void vk_result(VkResult err) {
	if (err == 0)
		return;
	debug_print_line(k_print_error, "Vulkan Error: %d\n", err);
	if (err < 0)
		abort();
}

typedef enum render_mode_type_t
{
	k_default_mode = 0,
	k_imgui_mode = 1
} render_mode_type_t;

typedef enum command_type_t
{
	k_command_frame_done,
	k_command_model,
	k_command_texture_model
	
} command_type_t;

typedef struct gpu_frame_t
{
	VkImage image;
	VkImageView view;
	VkFramebuffer frame_buffer;
	VkFence fence;
	gpu_cmd_buffer_t* cmd_buffer;
} gpu_frame_t;

typedef struct gpu_cmd_buffer_t
{
	VkCommandBuffer buffer;
	VkPipelineLayout pipeline_layout;
	int index_count;
	int vertex_count;
} gpu_cmd_buffer_t;

typedef struct model_command_t
{
	command_type_t type;
	ecs_entity_ref_t entity;
	gpu_mesh_info_t* mesh;
	gpu_shader_info_t* shader;
	gpu_uniform_buffer_info_t uniform_buffer;
} model_command_t;

typedef struct model_texture_command_t
{
	command_type_t type;
	ecs_entity_ref_t entity;
	gpu_image_mesh_info_t* mesh;
	gpu_shader_info_t* shader;
	gpu_uniform_buffer_info_t uniform_buffer;
} model_texture_command_t;

typedef struct frame_done_command_t
{
	command_type_t type;
} frame_done_command_t;

typedef struct draw_instance_t
{
	ecs_entity_ref_t entity;
	gpu_uniform_buffer_t** uniform_buffers;
	gpu_descriptor_t** descriptors;
	int frame_counter;
} draw_instance_t;

typedef struct draw_mesh_t
{
	gpu_mesh_info_t* info;
	gpu_mesh_t* mesh;
	int frame_counter;
} draw_mesh_t;

typedef struct draw_texture_mesh_t
{
	gpu_image_mesh_info_t* info;
	gpu_texture_mesh_t* mesh;
	int frame_counter;
} draw_texture_mesh_t;

typedef struct draw_shader_t
{
	gpu_shader_info_t* info;
	gpu_shader_t* shader;
	gpu_pipeline_t* pipeline;
	int frame_counter;
} draw_shader_t;

typedef struct render_t
{
	heap_t* heap;
	wm_window_t* window;
	thread_t* thread;
	gpu_t* gpu;
	queue_t* queue;

	int frame_counter;
	int gpu_frame_count;

	int instance_count;
	int mesh_count;
	int shader_count;
	draw_instance_t instances[k_render_max_drawables];
	draw_mesh_t meshes[k_render_max_drawables];
	draw_texture_mesh_t texture_meshes[k_render_max_drawables];
	draw_shader_t shaders[k_render_max_drawables];

	render_mode_type_t render_mode;
	ImGui_ImplVulkanH_Window main_window_data;

} render_t;

static int render_thread_func(void* user);

static draw_shader_t* create_or_get_shader_for_model_command(render_t* render, model_command_t* command);
static draw_shader_t* create_or_get_shader_for_texture_model_command(render_t* render, model_texture_command_t* command);

static draw_mesh_t* create_or_get_mesh_for_model_command(render_t* render, model_command_t* command);
static draw_texture_mesh_t* create_or_get_mesh_for_texture_model_command(render_t* render, model_texture_command_t* command);

static draw_instance_t* create_or_get_instance_for_model_command(render_t* render, model_command_t* command, gpu_shader_t* shader);
static draw_instance_t* create_or_get_instance_for_texture_model_command(render_t* render, gpu_texture_mesh_t* mesh, model_texture_command_t* command, gpu_shader_t* shader);

static void destroy_stale_data(render_t* render);

render_t* render_create(heap_t* heap, wm_window_t* window, bool render_imgui)
{
	render_t* render = heap_alloc(heap, sizeof(render_t), 8);
	render->heap = heap;
	render->window = window;
	render->queue = queue_create(heap, 3);
	render->frame_counter = 0;
	render->instance_count = 0;
	render->mesh_count = 0;
	render->shader_count = 0;
	render->thread = thread_create(render_thread_func, render);
	render->render_mode = (render_imgui) ? k_imgui_mode : k_default_mode;
	return render;
}

void render_destroy(render_t* render)
{
	queue_push(render->queue, NULL);
	thread_destroy(render->thread);
	heap_free(render->heap, render);
}

void render_push_model(render_t* render, ecs_entity_ref_t* entity, gpu_mesh_info_t* mesh, gpu_shader_info_t* shader, gpu_uniform_buffer_info_t* uniform)
{
	model_command_t* command = heap_alloc(render->heap, sizeof(model_command_t), 8);
	command->type = k_command_model;
	command->entity = *entity;
	command->mesh = mesh;
	command->shader = shader;
	command->uniform_buffer.size = uniform->size;
	command->uniform_buffer.data = heap_alloc(render->heap, uniform->size, 8);
	memcpy(command->uniform_buffer.data, uniform->data, uniform->size);
	queue_push(render->queue, command);
}

void render_push_model_image(render_t* render, ecs_entity_ref_t* entity, gpu_image_mesh_info_t* mesh, gpu_shader_info_t* shader, gpu_uniform_buffer_info_t* uniform)
{
	model_texture_command_t* command = heap_alloc(render->heap, sizeof(model_texture_command_t), 8);
	command->type = k_command_texture_model;
	command->entity = *entity;
	command->mesh = mesh;
	command->shader = shader;
	command->uniform_buffer.size = uniform->size;
	command->uniform_buffer.data = heap_alloc(render->heap, uniform->size, 8);
	memcpy(command->uniform_buffer.data, uniform->data, uniform->size);
	queue_push(render->queue, command);
}

void render_push_done(render_t* render)
{
	frame_done_command_t* command = heap_alloc(render->heap, sizeof(frame_done_command_t), 8);
	command->type = k_command_frame_done;
	queue_push(render->queue, command);
}

static int render_thread_func(void* user)
{
	render_t* render = user;

	render->gpu = gpu_create(render->heap, render->window);
	render->gpu_frame_count = gpu_get_frame_count(render->gpu);

	gpu_cmd_buffer_t* cmdbuf = NULL;
	gpu_pipeline_t* last_pipeline = NULL;
	gpu_mesh_t* last_mesh = NULL;
	gpu_texture_mesh_t* last_texture_mesh = NULL;
	int frame_index = 0;

	if (render->render_mode == k_imgui_mode) {

		// Setup Dear ImGui binding
		IMGUI_CHECKVERSION();

		ImGuiContext* ctx = igCreateContext(0);
		igSetCurrentContext(ctx);

		ImGuiIO* io = igGetIO();
		io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// Init for Win32
		ImGui_ImplWin32_Init(wm_get_hwnd(render->window));

		// Init for Vulkan
		ImGui_ImplVulkan_InitInfo init_info = {
			.Instance = gpu_get_instance(render->gpu),
			.PhysicalDevice = gpu_get_physical_devices(render->gpu),
			.Device = gpu_get_logical_devices(render->gpu), 
			.QueueFamily = (uint32_t)-1,
			.Queue = gpu_get_queue(render->gpu),
			.PipelineCache = gpu_get_pipeline_cache(render->gpu),
			.DescriptorPool = gpu_get_descriptor_pool(render->gpu),
			.Allocator = gpu_get_allocator(render->gpu),
			.MinImageCount = 2,
			.ImageCount = UINT8_MAX	,
			.CheckVkResultFn = vk_result
		};

		ImGui_ImplVulkan_Init(&init_info, gpu_get_render_pass(render->gpu));

		// create a command buffer (for texture/font)
		VkCommandBuffer commandBuffer;
		{
			VkCommandBufferAllocateInfo allocation_info =
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandPool = gpu_get_command_pool(render->gpu),
				.commandBufferCount = 1
			};

			VkResult result = vkAllocateCommandBuffers(gpu_get_logical_devices(render->gpu), &allocation_info, &commandBuffer);

			if (result != VK_SUCCESS) {
				debug_print_line(k_print_error, "vkAllocateCommandBuffers failed: %d\n", result);
			}

			VkCommandBufferBeginInfo begin_info =
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};

			vkBeginCommandBuffer(commandBuffer, &begin_info);
		}

		// we need to create a font texture
		ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

		// end command buffer
		{
			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submit_info = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &commandBuffer
			};

			vkQueueSubmit(gpu_get_queue(render->gpu), 1, &submit_info, VK_NULL_HANDLE);
			vkQueueWaitIdle(gpu_get_queue(render->gpu));

			vkFreeCommandBuffers(gpu_get_logical_devices(render->gpu), gpu_get_command_pool(render->gpu), 1, &commandBuffer);
		}

		vkDeviceWaitIdle(gpu_get_logical_devices(render->gpu));

		ImGui_ImplVulkan_DestroyFontUploadObjects();

		igStyleColorsDark(NULL);

	}

	while (true)
	{
		command_type_t* type = queue_pop(render->queue);
		if (!type)
		{
			break;
		}

		if (!cmdbuf)
		{
			cmdbuf = gpu_frame_begin(render->gpu);
		}

		

		if (*type == k_command_frame_done)
		{

			gpu_frame_end(render->gpu);
			cmdbuf = NULL;
			last_pipeline = NULL;
			last_mesh = NULL;
			last_texture_mesh = NULL;

			destroy_stale_data(render);
			++render->frame_counter;
			frame_index = render->frame_counter % render->gpu_frame_count;
		}
		else if (*type == k_command_model)
		{
			model_command_t* command = (model_command_t*)type;
			draw_shader_t* shader = create_or_get_shader_for_model_command(render, command);
			draw_mesh_t* mesh = create_or_get_mesh_for_model_command(render, command);
			draw_instance_t* instance = create_or_get_instance_for_model_command(render, command, shader->shader);

			heap_free(render->heap, command->uniform_buffer.data);


			if (last_pipeline != shader->pipeline)
			{
				gpu_cmd_pipeline_bind(render->gpu, cmdbuf, shader->pipeline);
				last_pipeline = shader->pipeline;
			}
			if (last_mesh != mesh->mesh)
			{
				gpu_cmd_mesh_bind(render->gpu, cmdbuf, mesh->mesh);
				last_mesh = mesh->mesh;
			}

			gpu_cmd_descriptor_bind(render->gpu, cmdbuf, instance->descriptors[frame_index]);
			gpu_cmd_draw(render->gpu, cmdbuf);

			if (render->render_mode == k_imgui_mode) {

				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplWin32_NewFrame();
				igNewFrame();

				// window
				igShowDemoWindow(true);

				// render
				igRender();
				ImDrawData* draw_data = igGetDrawData();
				ImGui_ImplVulkan_RenderDrawData(draw_data, cmdbuf->buffer);
			}
		}
		else if (*type == k_command_texture_model)
		{
			model_texture_command_t* command = (model_texture_command_t*)type;
			draw_shader_t* shader = create_or_get_shader_for_texture_model_command(render, command);
			draw_texture_mesh_t* mesh = create_or_get_mesh_for_texture_model_command(render, command);
			draw_instance_t* instance = create_or_get_instance_for_texture_model_command(render, mesh->mesh, command, shader->shader);

			heap_free(render->heap, command->uniform_buffer.data);

			if (last_pipeline != shader->pipeline)
			{
				gpu_cmd_pipeline_bind(render->gpu, cmdbuf, shader->pipeline);
				last_pipeline = shader->pipeline;
			}
			if (last_texture_mesh != mesh->mesh)
			{
				gpu_cmd_texture_mesh_bind(render->gpu, cmdbuf, mesh->mesh);
				last_texture_mesh = mesh->mesh;
			}
			gpu_cmd_descriptor_bind(render->gpu, cmdbuf, instance->descriptors[frame_index]);
			gpu_cmd_draw(render->gpu, cmdbuf);
		}

		heap_free(render->heap, type);
	}

	gpu_wait_until_idle(render->gpu);
	render->frame_counter += render->gpu_frame_count + 1;
	destroy_stale_data(render);

	gpu_destroy(render->gpu);
	render->gpu = NULL;

	return 0;
}

static draw_shader_t* create_or_get_shader_for_model_command(render_t* render, model_command_t* command)
{
	draw_shader_t* shader = NULL;
	for (int i = 0; i < render->shader_count; ++i)
	{
		if (render->shaders[i].info == command->shader)
		{
			shader = &render->shaders[i];
			break;
		}
	}
	if (!shader)
	{
		assert(render->shader_count < _countof(render->shaders));
		shader = &render->shaders[render->shader_count++];
		shader->info = command->shader;
	}
	if (!shader->shader)
	{
		shader->shader = gpu_shader_create(render->gpu, shader->info);
	}
	if (!shader->pipeline)
	{
		gpu_pipeline_info_t pipeline_info =
		{
			.shader = shader->shader,
			.mesh_layout = command->mesh->layout,
		};
		shader->pipeline = gpu_pipeline_create(render->gpu, &pipeline_info);
	}
	shader->frame_counter = render->frame_counter;
	return shader;
}

static draw_shader_t* create_or_get_shader_for_texture_model_command(render_t* render, model_texture_command_t* command)
{
	draw_shader_t* shader = NULL;
	for (int i = 0; i < render->shader_count; ++i)
	{
		if (render->shaders[i].info == command->shader)
		{
			shader = &render->shaders[i];
			break;
		}
	}
	if (!shader)
	{
		assert(render->shader_count < _countof(render->shaders));
		shader = &render->shaders[render->shader_count++];
		shader->info = command->shader;
	}
	if (!shader->shader)
	{
		shader->shader = gpu_shader_create(render->gpu, shader->info);
	}
	if (!shader->pipeline)
	{
		gpu_pipeline_info_t pipeline_info =
		{
			.shader = shader->shader,
			.mesh_layout = command->mesh->layout,
		};
		shader->pipeline = gpu_pipeline_create(render->gpu, &pipeline_info);
	}
	shader->frame_counter = render->frame_counter;
	return shader;
}

static draw_mesh_t* create_or_get_mesh_for_model_command(render_t* render, model_command_t* command)
{
	draw_mesh_t* mesh = NULL;
	for (int i = 0; i < render->mesh_count; ++i)
	{
		if (render->meshes[i].info == command->mesh)
		{
			mesh = &render->meshes[i];
			break;
		}
	}
	if (!mesh)
	{
		assert(render->mesh_count < _countof(render->meshes));
		mesh = &render->meshes[render->mesh_count++];
		mesh->info = command->mesh;
	}
	if (!mesh->mesh)
	{
		mesh->mesh = gpu_mesh_create(render->gpu, command->mesh);
	}
	mesh->frame_counter = render->frame_counter;
	return mesh;
}

static draw_texture_mesh_t* create_or_get_mesh_for_texture_model_command(render_t* render, model_texture_command_t* command)
{
	draw_texture_mesh_t* mesh = NULL;
	for (int i = 0; i < render->mesh_count; ++i)
	{
		if (render->texture_meshes[i].info == command->mesh)
		{
			mesh = &render->texture_meshes[i];
			break;
		}
	}
	if (!mesh)
	{
		assert(render->mesh_count < _countof(render->texture_meshes));
		mesh = &render->texture_meshes[render->mesh_count++];
		mesh->info = command->mesh;
	}
	if (!mesh->mesh)
	{
		mesh->mesh = gpu_texture_mesh_create(render->gpu, command->mesh);
	}
	mesh->frame_counter = render->frame_counter;
	return mesh;
}

static draw_instance_t* create_or_get_instance_for_model_command(render_t* render, model_command_t* command, gpu_shader_t* shader)
{
	draw_instance_t* instance = NULL;
	for (int i = 0; i < render->instance_count; ++i)
	{
		if (memcmp(&render->instances[i].entity, &command->entity, sizeof(ecs_entity_ref_t)) == 0)
		{
			instance = &render->instances[i];
			break;
		}
	}
	if (!instance)
	{
		assert(render->instance_count < _countof(render->instances));
		instance = &render->instances[render->instance_count++];

		instance->entity = command->entity;
		instance->uniform_buffers = heap_alloc(render->heap, sizeof(gpu_uniform_buffer_t*) * render->gpu_frame_count, 8);
		instance->descriptors = heap_alloc(render->heap, sizeof(gpu_descriptor_t*) * render->gpu_frame_count, 8);
		for (int i = 0; i < render->gpu_frame_count; ++i)
		{
			instance->uniform_buffers[i] = gpu_uniform_buffer_create(render->gpu, &command->uniform_buffer);

			gpu_descriptor_info_t descriptor_info =
			{
				.shader = shader,
				.uniform_buffers = &instance->uniform_buffers[i],
				.uniform_buffer_count = 1,
			};
			instance->descriptors[i] = gpu_descriptor_create(render->gpu, &descriptor_info);
		}
	}

	int frame_index = render->frame_counter % render->gpu_frame_count;
	gpu_uniform_buffer_update(render->gpu, instance->uniform_buffers[frame_index], command->uniform_buffer.data, command->uniform_buffer.size);

	instance->frame_counter = render->frame_counter;

	return instance;
}

static draw_instance_t* create_or_get_instance_for_texture_model_command(render_t* render, gpu_texture_mesh_t* mesh, model_texture_command_t* command, gpu_shader_t* shader)
{
	draw_instance_t* instance = NULL;
	for (int i = 0; i < render->instance_count; ++i)
	{
		if (memcmp(&render->instances[i].entity, &command->entity, sizeof(ecs_entity_ref_t)) == 0)
		{
			instance = &render->instances[i];
			break;
		}
	}
	if (!instance)
	{
		assert(render->instance_count < _countof(render->instances));
		instance = &render->instances[render->instance_count++];

		instance->entity = command->entity;
		instance->uniform_buffers = heap_alloc(render->heap, sizeof(gpu_uniform_buffer_t*) * render->gpu_frame_count, 8);
		instance->descriptors = heap_alloc(render->heap, sizeof(gpu_descriptor_t*) * render->gpu_frame_count, 8);
		for (int i = 0; i < render->gpu_frame_count; ++i)
		{
			instance->uniform_buffers[i] = gpu_uniform_buffer_create(render->gpu, &command->uniform_buffer);

			gpu_descriptor_info_t descriptor_info =
			{
				.shader = shader,
				.uniform_buffers = &instance->uniform_buffers[i],
				.uniform_buffer_count = 2,
			};
			instance->descriptors[i] = gpu_descriptor_create_texture(render->gpu, mesh, &descriptor_info, k_texture_sampler_binding);
		}
	}

	int frame_index = render->frame_counter % render->gpu_frame_count;
	gpu_uniform_buffer_update(render->gpu, instance->uniform_buffers[frame_index], command->uniform_buffer.data, command->uniform_buffer.size);

	instance->frame_counter = render->frame_counter;

	return instance;
}

static void destroy_stale_data(render_t* render)
{
	for (int i = render->instance_count - 1; i >= 0; --i)
	{
		if (render->instances[i].frame_counter + render->gpu_frame_count <= render->frame_counter)
		{
			for (int f = 0; f < render->gpu_frame_count; ++f)
			{
				gpu_descriptor_destroy(render->gpu, render->instances[i].descriptors[f]);
				gpu_uniform_buffer_destroy(render->gpu, render->instances[i].uniform_buffers[f]);
			}
			render->instances[i] = render->instances[render->instance_count - 1];
			render->instance_count--;
		}
	}
	for (int i = render->mesh_count - 1; i >= 0; --i)
	{
		if (render->texture_meshes[i].frame_counter + render->gpu_frame_count <= render->frame_counter)
		{
			gpu_mesh_destroy(render->gpu, render->texture_meshes[i].mesh);
			render->texture_meshes[i] = render->texture_meshes[render->mesh_count - 1];
			render->mesh_count--;
		}
	}
	for (int i = render->mesh_count - 1; i >= 0; --i)
	{
		if (render->meshes[i].frame_counter + render->gpu_frame_count <= render->frame_counter)
		{
			gpu_mesh_destroy(render->gpu, render->meshes[i].mesh);
			render->meshes[i] = render->meshes[render->mesh_count - 1];
			render->mesh_count--;
		}
	}
	for (int i = render->shader_count - 1; i >= 0; --i)
	{
		if (render->shaders[i].frame_counter + render->gpu_frame_count <= render->frame_counter)
		{
			gpu_pipeline_destroy(render->gpu, render->shaders[i].pipeline);
			gpu_shader_destroy(render->gpu, render->shaders[i].shader);
			render->shaders[i] = render->shaders[render->shader_count - 1];
			render->shader_count--;
		}
	}
}

gpu_t* render_get_gpu(render_t* render) {
	return render->gpu;
}