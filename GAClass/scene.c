#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "debug.h"
#include "vec3f.h"
#include "scene.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_vulkan.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#define IM_UNUSED(_VAR)  ((void)(_VAR))
#define IM_ASSERT(_EXPR) assert(_EXPR)
#define IM_ARRAYSIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*(_ARR))))

static VkAllocationCallbacks* g_Allocator = NULL;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t                 g_MinImageCount = 2;
static bool                     g_SwapChainRebuild = false;

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

typedef struct boundary_t {
	float x_pos;
	float y_pos;
	float x_neg;
	float y_neg;
	float z_pos;
	float z_neg;
} boundary_t;

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct model_texture_component_t
{
	gpu_image_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_texture_component_t;

typedef struct player_component_t
{
	int index;
	boundary_t boundary;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct ui_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} ui_component_t;

// Axis Aligned Bounding Boxes
typedef struct collider_component_t {
	// Note that we are using transform without a pointer or in its own component, so colliders can have
	// their own behavior when outside of any component that it is supposed to use
	// i.e. player components will have to control the transform of the collider
	transform_t transform;
	vec3f_t component_size;
} collider_component_t;

// from ecs.c (max entities = 512)
enum
{
	k_max_entities = 512,
};

typedef struct scene_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int model_texture_type;
	int name_type;
	int collider_type;
	int ui_type;

	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t current_entity;

	ecs_entity_ref_t ui_ent;

	ecs_entity_ref_t all_ent[k_max_entities];
	int next_free_entity;

	gpu_image_mesh_info_t ui_mesh;
	gpu_shader_info_t ui_shader;

	gpu_mesh_info_t object_mesh;
	gpu_shader_info_t object_shader;

	gpu_mesh_info_t cube_mesh;
	gpu_shader_info_t cube_shader;

	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;

	SDL_Window* imgui_window;
	ImVec4 clearColor;
	ImGui_ImplVulkanH_Window* wd;
} scene_t;

// general
static void draw_models(scene_t* scene);
static void unload_shader_resources(scene_t* scene);

// camera
static void spawn_camera(scene_t* scene);
static void update_camera(scene_t* scene);

// scene hierarchy (OLD UI)
static void load_scene_hierarchy_resources(scene_t* scene, const char* image_location);
static void spawn_scene_hierarchy(scene_t* scene);
static void update_scene_hierarchy(scene_t* scene);

static void scene_interaction(scene_t* scene);

// UI
static void spawn_ui(scene_t* scene);

// component editing/adding
static void replace_name(scene_t* scene, ecs_entity_ref_t entity, const char* new_name);
static void add_collider(scene_t* scene, ecs_entity_ref_t entity);

// add a blank object
static void load_object_scene_resources(scene_t* scene);
static ecs_entity_ref_t add_object_to_scene(scene_t* scene);
static void add_entity_type_to_object(scene_t* scene, ecs_entity_ref_t entity, int entity_type);

// boundary
static bool in_boundary(boundary_t boundary, transform_t transform);
static void create_boundaries(boundary_t* boundary, float x_pos, float y_pos, float x_neg, float y_neg, float z_pos, float z_neg);

// check if a specific boundary position is in the positive or negative bounds
static bool in_boundary_pos_x(boundary_t boundary, transform_t transform);
static bool in_boundary_pos_y(boundary_t boundary, transform_t transform);
static bool in_boundary_pos_z(boundary_t boundary, transform_t transform);
static bool in_boundary_neg_x(boundary_t boundary, transform_t transform);
static bool in_boundary_neg_y(boundary_t boundary, transform_t transform);
static bool in_boundary_neg_z(boundary_t boundary, transform_t transform);

// colliders
static bool check_collision(collider_component_t one, collider_component_t two);
static vec3f_t get_collision_min(collider_component_t col);
static vec3f_t get_collision_max(collider_component_t col);

static void InitIMGUI(scene_t* scene);
static void IMGUI_HIERARCHY(scene_t* scene);

// controls
static void move_object_x_up(scene_t* scene, float dt);
static void move_object_x_down(scene_t* scene, float dt);
static void move_object_y_up(scene_t* scene, float dt);
static void move_object_y_down(scene_t* scene, float dt);
static void move_object_z_up(scene_t* scene, float dt);
static void move_object_z_down(scene_t* scene, float dt);
static void rotate_object_x_up(scene_t* scene, float dt);
static void rotate_object_x_down(scene_t* scene, float dt);
static void rotate_object_y_up(scene_t* scene, float dt);
static void rotate_object_y_down(scene_t* scene, float dt);
static void rotate_object_z_up(scene_t* scene, float dt);
static void rotate_object_z_down(scene_t* scene, float dt);

// ===========================================================================================
//                                           GENERAL
// ===========================================================================================

scene_t* scene_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	scene_t* scene = heap_alloc(heap, sizeof(scene_t), 8);
	scene->heap = heap;
	scene->fs = fs;
	scene->window = window;
	scene->render = render;
	scene->next_free_entity = 0;

	scene->timer = timer_object_create(heap, NULL);

	scene->ecs = ecs_create(heap);
	scene->transform_type = ecs_register_component_type(scene->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	scene->camera_type = ecs_register_component_type(scene->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	scene->model_type = ecs_register_component_type(scene->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	scene->name_type = ecs_register_component_type(scene->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	scene->collider_type = ecs_register_component_type(scene->ecs, "collider", sizeof(collider_component_t), _Alignof(collider_component_t));
	scene->model_texture_type = ecs_register_component_type(scene->ecs, "model texture", sizeof(model_texture_component_t), _Alignof(model_texture_component_t));
	scene->ui_type = ecs_register_component_type(scene->ecs, "ui", sizeof(ui_component_t), _Alignof(ui_component_t));
	
	InitIMGUI(scene);

	// load_scene_hierarchy_resources(scene, "resources/smile.jpg");
	load_object_scene_resources(scene);

	// camera
	spawn_camera(scene);

	// UI
	// spawn_ui(scene);

	scene->current_entity = dummy_entity;

	return scene;
}

static void SetupVulkan(const char** extensions, uint32_t extensions_count)
{
	VkResult err;

	// Create Vulkan Instance
	VkInstanceCreateInfo inst_create_info = {
	  .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	  .enabledExtensionCount = extensions_count,
	  .ppEnabledExtensionNames = extensions,
	};
#ifdef IMGUI_VULKAN_DEBUG_REPORT
	// Enabling validation layers
	const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
	inst_create_info.enabledLayerCount = 1;
	inst_create_info.ppEnabledLayerNames = layers;

	// Enable debug report extension (we need additional storage, so we duplicate the user array to add our new extension to it)
	const char** extensions_ext = (const char**)malloc(
		sizeof(const char*) * (extensions_count + 1));
	memcpy(extensions_ext, extensions, extensions_count * sizeof(const char*));
	extensions_ext[extensions_count] = "VK_EXT_debug_report";
	inst_create_info.enabledExtensionCount = extensions_count + 1;
	inst_create_info.ppEnabledExtensionNames = extensions_ext;

	// Create Vulkan Instance
	err = vkCreateInstance(&inst_create_info, g_Allocator, &g_Instance);
	check_vk_result(err);
	free(extensions_ext);

	// Get the function pointer (required for any extensions)
	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
		(PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance,
			"vkCreateDebugReportCallbackEXT");
	IM_ASSERT(vkCreateDebugReportCallbackEXT != NULL);

	// Setup the debug report callback
	VkDebugReportCallbackCreateInfoEXT debug_report_ci = {
	  .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
	  .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
			   VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
	  .pfnCallback = debug_report,
	  .pUserData = NULL,
	};
	err = vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator,
		&g_DebugReport);
	check_vk_result(err);
#else
	// Create Vulkan Instance without any debug feature
	err = vkCreateInstance(&inst_create_info, g_Allocator, &g_Instance);
	check_vk_result(err);
	IM_UNUSED(g_DebugReport);
#endif

	// Select GPU
	uint32_t gpu_count;
	err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, NULL);
	check_vk_result(err);
	IM_ASSERT(gpu_count > 0);

	VkPhysicalDevice* gpus = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * gpu_count);
	err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, gpus);
	check_vk_result(err);

	// If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
	// most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
	// dedicated GPUs) is out of scope of this sample.
	int use_gpu = 0;
	for (int i = 0; i < (int)gpu_count; i++)
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(gpus[i], &properties);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			use_gpu = i;
			break;
		}
	}

	g_PhysicalDevice = gpus[use_gpu];
	free(gpus);

	// Select graphics queue family
	uint32_t count;
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, NULL);
	VkQueueFamilyProperties* queues = (VkQueueFamilyProperties*)malloc(
		sizeof(VkQueueFamilyProperties) * count);
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues);
	for (uint32_t i = 0; i < count; i++)
		if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			g_QueueFamily = i;
			break;
		}
	free(queues);
	IM_ASSERT(g_QueueFamily != (uint32_t)-1);

	// Create Logical Device (with 1 queue)
	int device_extension_count = 1;
	const char* device_extensions[] = { "VK_KHR_swapchain" };
	const float queue_priority[] = { 1.0f };
	VkDeviceQueueCreateInfo queue_info[1] = {
	  [0] .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	  [0].queueFamilyIndex = g_QueueFamily,
	  [0].queueCount = 1,
	  [0].pQueuePriorities = queue_priority,
	};
	VkDeviceCreateInfo create_info = {
	  .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	  .queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]),
	  .pQueueCreateInfos = queue_info,
	  .enabledExtensionCount = device_extension_count,
	  .ppEnabledExtensionNames = device_extensions,
	};
	err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
	check_vk_result(err);
	vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);

	// Create Descriptor Pool
	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};
	VkDescriptorPoolCreateInfo pool_info = {
	  .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	  .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
	  .maxSets = 1000 * IM_ARRAYSIZE(pool_sizes),
	  .poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes),
	  .pPoolSizes = pool_sizes,
	};
	err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
	check_vk_result(err);
}

// All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// Your real engine/app may not use them.
static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface,
	int width, int height)
{
	wd->Surface = surface;

	// Check for WSI support
	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
	if (res != VK_TRUE)
	{
		fprintf(stderr, "Error no WSI support on physical device 0\n");
		exit(-1);
	}

	// Select Surface Format
	const VkFormat requestSurfaceImageFormat[] = {
		VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
	};
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
		g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat,
		(size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

	// Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
	VkPresentModeKHR present_modes[] = {
	  VK_PRESENT_MODE_MAILBOX_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR
	};
#else
	VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
	wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
		g_PhysicalDevice, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));

	// Create SwapChain, RenderPass, Framebuffer, etc.
	IM_ASSERT(g_MinImageCount >= 2);
	ImGui_ImplVulkanH_CreateOrResizeWindow(
		g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator,
		width, height, g_MinImageCount);
}

static void CleanupVulkan()
{
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
	// Remove the debug report callback
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
		(PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
	vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // IMGUI_VULKAN_DEBUG_REPORT

	vkDestroyDevice(g_Device, g_Allocator);
	vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow()
{
	ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
	VkResult err;

	VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
	{
		g_SwapChainRebuild = true;
		return;
	}
	check_vk_result(err);

	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
	err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
	check_vk_result(err);

	err = vkResetFences(g_Device, 1, &fd->Fence);
	check_vk_result(err);

	err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
	check_vk_result(err);
	VkCommandBufferBeginInfo info = {
	  .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	  .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
	check_vk_result(err);
	VkRenderPassBeginInfo rp_info = {
	  .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
	  .renderPass = wd->RenderPass,
	  .framebuffer = fd->Framebuffer,
	  .renderArea.extent.width = wd->Width,
	  .renderArea.extent.height = wd->Height,
	  .clearValueCount = 1,
	  .pClearValues = &wd->ClearValue,
	};
	vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer, VK_NULL_HANDLE);

	// Submit command buffer
	vkCmdEndRenderPass(fd->CommandBuffer);
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo sub_info = {
	  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	  .waitSemaphoreCount = 1,
	  .pWaitSemaphores = &image_acquired_semaphore,
	  .pWaitDstStageMask = &wait_stage,
	  .commandBufferCount = 1,
	  .pCommandBuffers = &fd->CommandBuffer,
	  .signalSemaphoreCount = 1,
	  .pSignalSemaphores = &render_complete_semaphore,
	};

	err = vkEndCommandBuffer(fd->CommandBuffer);
	check_vk_result(err);
	err = vkQueueSubmit(g_Queue, 1, &sub_info, fd->Fence);
	check_vk_result(err);
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd)
{
	if (g_SwapChainRebuild) return;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	VkPresentInfoKHR info = {
	  .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	  .waitSemaphoreCount = 1,
	  .pWaitSemaphores = &render_complete_semaphore,
	  .swapchainCount = 1,
	  .pSwapchains = &wd->Swapchain,
	  .pImageIndices = &wd->FrameIndex,
	};
	VkResult err = vkQueuePresentKHR(g_Queue, &info);
	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
	{
		g_SwapChainRebuild = true;
		return;
	}
	check_vk_result(err);
	wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
}

static void InitIMGUI(scene_t* scene) {
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		SDL_Log("failed to init: %s", SDL_GetError());
		return -1;
	}
	
	/// Setup window
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN |
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	scene->imgui_window = SDL_CreateWindow("Scene Editor Window",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, window_flags);
	if (scene->imgui_window == NULL) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		return -1;
	}

	// Setup Vulkan
	uint32_t extensions_count = 0;
	SDL_Vulkan_GetInstanceExtensions(scene->imgui_window, &extensions_count, NULL);
	const char** extensions = malloc(extensions_count * sizeof(const char*));
	SDL_Vulkan_GetInstanceExtensions(scene->imgui_window, &extensions_count, extensions);
	SetupVulkan(extensions, extensions_count);
	free(extensions);

	// Create Window Surface
	VkSurfaceKHR surface;
	VkResult err;
	if (SDL_Vulkan_CreateSurface(scene->imgui_window, g_Instance, &surface) == 0)
	{
		printf("Failed to create Vulkan surface.\n");
		return 1;
	}

	// Create Framebuffers
	int w, h;
	SDL_GetWindowSize(scene->imgui_window, &w, &h);
	scene->wd = &g_MainWindowData;
	SetupVulkanWindow(scene->wd, surface, w, h);

	// setup imgui
	igCreateContext(NULL);

	//set docking
	ImGuiIO* ioptr = igGetIO();
	ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	//ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForVulkan(scene->imgui_window);
	ImGui_ImplVulkan_InitInfo init_info = {
	  .Instance = g_Instance,
	  .PhysicalDevice = g_PhysicalDevice,
	  .Device = g_Device,
	  .QueueFamily = g_QueueFamily,
	  .Queue = g_Queue,
	  .PipelineCache = g_PipelineCache,
	  .DescriptorPool = g_DescriptorPool,
	  .Subpass = 0,
	  .MinImageCount = g_MinImageCount,
	  .ImageCount = scene->wd->ImageCount,
	  .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
	  .Allocator = g_Allocator,
	  .CheckVkResultFn = check_vk_result
	};
	ImGui_ImplVulkan_Init(&init_info, scene->wd->RenderPass);

	igStyleColorsDark(NULL);

	// Upload Fonts
	// Use any command queue
	VkCommandPool command_pool = scene->wd->Frames[scene->wd->FrameIndex].CommandPool;
	VkCommandBuffer command_buffer = scene->wd->Frames[scene->wd->FrameIndex].CommandBuffer;

	err = vkResetCommandPool(g_Device, command_pool, 0);
	check_vk_result(err);
	VkCommandBufferBeginInfo begin_info = {
	  .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	  .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	err = vkBeginCommandBuffer(command_buffer, &begin_info);
	check_vk_result(err);

	ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

	VkSubmitInfo end_info = {
	  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	  .commandBufferCount = 1,
	  .pCommandBuffers = &command_buffer,
	};
	err = vkEndCommandBuffer(command_buffer);
	check_vk_result(err);
	err = vkQueueSubmit(g_Queue, 1, &end_info, VK_NULL_HANDLE);
	check_vk_result(err);

	err = vkDeviceWaitIdle(g_Device);
	check_vk_result(err);
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	ImVec4 clearColor;
	clearColor.x = 0.45f;
	clearColor.y = 0.55f;
	clearColor.z = 0.60f;
	clearColor.w = 1.00f;

	scene->clearColor = clearColor;
}

static void UpdateIMGUI(scene_t* scene) {
	SDL_Event e;

	// we need to call SDL_PollEvent to let window rendered, otherwise
	// no window will be shown
	while (SDL_PollEvent(&e) != 0)
	{
		ImGui_ImplSDL2_ProcessEvent(&e);
		if (e.type == SDL_QUIT)
			return;
		if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
			e.window.windowID == SDL_GetWindowID(scene->window))
			return;
	}
	// Resize swap chain
	if (g_SwapChainRebuild)
	{
		int width, height;
		SDL_GetWindowSize(scene->window, &width, &height);
		if (width > 0 && height > 0)
		{
			ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
			ImGui_ImplVulkanH_CreateOrResizeWindow(
				g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData,
				g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
			g_MainWindowData.FrameIndex = 0;
			g_SwapChainRebuild = false;
		}
	}

	// start imgui frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	igNewFrame();

	IMGUI_HIERARCHY(scene);

	// render
	igRender();
	ImDrawData* draw_data = igGetDrawData();
	const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
	if (!is_minimized)
	{
		scene->wd->ClearValue.color.float32[0] = scene->clearColor.x * scene->clearColor.w;
		scene->wd->ClearValue.color.float32[1] = scene->clearColor.y * scene->clearColor.w;
		scene->wd->ClearValue.color.float32[2] = scene->clearColor.z * scene->clearColor.w;
		scene->wd->ClearValue.color.float32[3] = scene->clearColor.w;
		FrameRender(scene->wd, draw_data);
		FramePresent(scene->wd);
	}
}

void update_next_entity_location(scene_t* scene) {
	int result = scene->next_free_entity;
	while (ecs_is_entity_ref_valid(scene->ecs,
		scene->all_ent[result],
		true)) {
		result++;
	}
	scene->next_free_entity = wrapi(result, 0, k_max_entities) + 1;
}

void scene_destroy(scene_t* scene)
{
	ecs_destroy(scene->ecs);
	timer_object_destroy(scene->timer);

	unload_shader_resources(scene);

	heap_free(scene->heap, scene);
}

void scene_update(scene_t* scene) {
	timer_object_update(scene->timer);
	ecs_update(scene->ecs);
	update_camera(scene);

	UpdateIMGUI(scene);

	scene_interaction(scene);

	draw_models(scene);
	render_push_done(scene->render);
}

static void draw_models(scene_t* scene)
{
	uint64_t k_camera_query_mask = (1ULL << scene->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(scene->ecs, k_camera_query_mask);
		ecs_query_is_valid(scene->ecs, &camera_query);
		ecs_query_next(scene->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(scene->ecs, &camera_query, scene->camera_type);

		uint64_t k_model_query_mask = (1ULL << scene->transform_type) | (1ULL << scene->model_type);
		for (ecs_query_t query = ecs_query_create(scene->ecs, k_model_query_mask);
			ecs_query_is_valid(scene->ecs, &query);
			ecs_query_next(scene->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(scene->ecs, &query, scene->transform_type);
			model_component_t* model_comp = ecs_query_get_component(scene->ecs, &query, scene->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(scene->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(scene->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}

		uint64_t k_image_model_query_mark = (1ULL << scene->transform_type) | (1ULL << scene->model_texture_type);
		for (ecs_query_t query = ecs_query_create(scene->ecs, k_image_model_query_mark);
			ecs_query_is_valid(scene->ecs, &query);
			ecs_query_next(scene->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(scene->ecs, &query, scene->transform_type);
			model_texture_component_t* model_comp = ecs_query_get_component(scene->ecs, &query, scene->model_texture_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(scene->ecs, &query);

			struct 
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model_image(scene->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}

		uint64_t k_ui_query_mark = (1ULL << scene->transform_type) | (1ULL << scene->ui_type);
		for (ecs_query_t query = ecs_query_create(scene->ecs, k_ui_query_mark);
			ecs_query_is_valid(scene->ecs, &query);
			ecs_query_next(scene->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(scene->ecs, &query, scene->transform_type);
			ui_component_t* ui_comp = ecs_query_get_component(scene->ecs, &query, scene->ui_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(scene->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			// add sampler
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model_imgui(scene->render, &entity_ref, ui_comp->mesh_info, ui_comp->shader_info, &uniform_info);
		}
	}
}


// ===========================================================================================
//                                           CAMERA
// ===========================================================================================

// generate a camera
static void spawn_camera(scene_t* scene)
{
	uint64_t k_camera_ent_mask =
		(1ULL << scene->camera_type) |
		(1ULL << scene->name_type) |
		(1ULL << scene->transform_type);
	scene->camera_ent = ecs_entity_add(scene->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->camera_ent, scene->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(scene->ecs, scene->camera_ent, scene->camera_type, true);
	mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->transform_type, true);
	transform_identity(&transform_comp->transform);

	transform_comp->transform.translation = eye_pos;

	update_next_entity_location(scene);
}

static void update_camera(scene_t* scene) {
	// update the transform in the camera to the view

	uint64_t k_query_mask = (1ULL << scene->transform_type) | (1ULL << scene->camera_type);

	for (ecs_query_t query = ecs_query_create(scene->ecs, k_query_mask);
		ecs_query_is_valid(scene->ecs, &query);
		ecs_query_next(scene->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(scene->ecs, &query, scene->transform_type);
		camera_component_t* camera_comp = ecs_query_get_component(scene->ecs, &query, scene->camera_type);

		vec3f_t eye_pos = transform_comp->transform.translation;
		vec3f_t forward = vec3f_forward(); // quatf_to_eulers(transform_comp->transform.rotation);
		vec3f_t up = vec3f_up();
		mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
	}
}

// ===========================================================================================
//												UI
// ===========================================================================================

static void spawn_ui(scene_t* scene) {
	uint64_t k_object_ent_mask =
		(1ULL << scene->transform_type) |
		(1ULL << scene->name_type) |
		(1ULL << scene->ui_type);
	scene->all_ent[scene->next_free_entity] = ecs_entity_add(scene->ecs, k_object_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->transform_type, true);
	transform_identity(&transform_comp->transform);

	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "UI");

	ui_component_t* ui_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->ui_type, true);
	ui_comp->mesh_info = &scene->cube_mesh;
	ui_comp->shader_info = &scene->cube_shader;

	update_next_entity_location(scene);

	return scene->all_ent[scene->next_free_entity];

}

// ===========================================================================================
//                                       SCENE HIERARCHY
// ===========================================================================================

static void spawn_scene_hierarchy(scene_t* scene) {
	uint64_t k_object_ent_mask =
		(1ULL << scene->transform_type) |
		(1ULL << scene->name_type) |
		(1ULL << scene->model_texture_type);
	scene->all_ent[scene->next_free_entity] = ecs_entity_add(scene->ecs, k_object_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->transform_type, true);
	transform_identity(&transform_comp->transform);

	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "UI");

	model_texture_component_t* model_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->model_texture_type, true);
	model_comp->mesh_info = &scene->ui_mesh;
	model_comp->shader_info = &scene->ui_shader;

	update_next_entity_location(scene);

	return scene->all_ent[scene->next_free_entity];

}

static void load_scene_hierarchy_resources(scene_t* scene, const char* image_location) {
	scene->vertex_shader_work = fs_read(scene->fs, "shaders/triangle-vert.spv", scene->heap, false, false);
	scene->fragment_shader_work = fs_read(scene->fs, "shaders/triangle-frag.spv", scene->heap, false, false);
	scene->ui_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(scene->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(scene->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(scene->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(scene->fragment_shader_work),
		.uniform_buffer_count = 2,
	};

	static vec3f_t plane_verts[] =
	{
		{  1.0f,  1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
		{ -1.0f,  1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
		{ -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
		{  1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }
	};

	static uint16_t plane_indices[] =
	{
		0, 1, 2, 2, 3, 0
	};

	scene->ui_mesh = (gpu_image_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_u44_c444_i2,
		.vertex_data = plane_verts,
		.vertex_data_size = sizeof(plane_verts),
		.index_data = plane_indices,
		.index_data_size = sizeof(plane_indices),
		.image_location = image_location,
		.image_data = NULL,
		.image_data_size = 0
	};
}

static void IMGUI_HIERARCHY(scene_t* scene) {

	igBegin("RE Engine Scene Hierarchy", NULL, 0);

	igText("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / igGetIO()->Framerate, igGetIO()->Framerate);

	float dt = (float)timer_object_get_delta_ms(scene->timer) * 0.001f;

	dt *= 1.0f;

	ImVec2 defButtonSize;
	defButtonSize.x = 0;
	defButtonSize.y = 0;

	ImGuiTabBarFlags flags;
	bool* p_open = heap_alloc(scene->heap, sizeof(bool), 1);
	*p_open = true;
	if (igBeginTabBar("Scene", NULL)) {
		
		if (igBeginTabItem("Scene", p_open, NULL)) {
			uint64_t k_query_mask = (1ULL << scene->name_type) | (1ULL << scene->transform_type);
			for (ecs_query_t query = ecs_query_create(scene->ecs, k_query_mask);
				ecs_query_is_valid(scene->ecs, &query);
				ecs_query_next(scene->ecs, &query))
			{
				ecs_entity_ref_t entity_ref = ecs_query_get_entity(scene->ecs, &query);
				name_component_t* name_comp = ecs_query_get_component(scene->ecs, &query, scene->name_type);
				transform_component_t* transform_comp = ecs_query_get_component(scene->ecs, &query, scene->transform_type);
				// entity
				{
					// Entity
					ImVec2 buttonSize;
					buttonSize.x = 0;
					buttonSize.y = 0;
					if (igButton(name_comp->name, buttonSize))
						scene->current_entity = entity_ref;
				}
			}
			igEndTabItem();
		}
		
		if (igBeginTabItem("Object", p_open, NULL)) {
			if (!ecs_entity_is_dummy_entity(scene->current_entity)) {
				name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->name_type, false);
				transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, false);

				// name
				igText(name_comp->name);

				char changeName[32];
				memset(changeName, 0, sizeof(changeName));
				//igInputText(changeName, name_comp->name, sizeof(changeName), 0, NULL, NULL);
				//strcpy_s(name_comp->name, sizeof(changeName), changeName);
				
				// transform
				{
					// position
					igText("Position <");
					igSameLine(0.0f, -1.0f);

					const char* transform_temp = heap_alloc(scene->heap, sizeof(float), 4);

					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.translation.x);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.translation.y);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float), "%f", transform_comp->transform.translation.z);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);
					igText(">");
					
					// rotation
					igText("Rotation <");
					igSameLine(0.0f, -1.0f);


					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.rotation.x);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.rotation.y);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float), "%f", transform_comp->transform.rotation.z);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);
					igText(">");

					// scale
					igText("Scale <");
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.scale.x);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float) + 1, "%f,", transform_comp->transform.scale.y);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);

					snprintf(transform_temp, sizeof(float), "%f", transform_comp->transform.scale.z);
					igText(transform_temp);
					igSameLine(0.0f, -1.0f);
					igText(">");

					// add a collider component
					if (igButton("Add Collider Component", defButtonSize))
						add_collider(scene, scene->current_entity);

					name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->name_type, false);
					if (name_comp != NULL) {
						igText("[Name Component]");
					}
					transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, false);
					if (transform_comp != NULL) {
						igText("[Transform Component]");
					}
					collider_component_t* collider_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->collider_type, false);
					if (collider_comp != NULL) {
						igText("[Collider Component]");
					}
					model_component_t* model_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->model_type, false);
					if (model_comp != NULL) {
						igText("[Model Component]");
					}
				}
			}
			// entity controls
			{
				igText("Change Position: ");
				igSameLine(0.0f, -1.0f);
				if (igButton("+X", defButtonSize))
					move_object_x_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-X", defButtonSize))
					move_object_x_down(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("+Y", defButtonSize))
					move_object_y_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-Y", defButtonSize))
					move_object_y_down(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("+Z", defButtonSize))
					move_object_z_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-Z", defButtonSize))
					move_object_z_down(scene, dt);

				igText("Change Rotation: ");
				igSameLine(0.0f, -1.0f);
				if (igButton("+X", defButtonSize))
					rotate_object_x_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-X", defButtonSize))
					rotate_object_x_down(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("+Y", defButtonSize))
					rotate_object_y_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-Y", defButtonSize))
					rotate_object_y_down(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("+Z", defButtonSize))
					rotate_object_z_up(scene, dt);
				igSameLine(0.0f, -1.0f);
				if (igButton("-Z", defButtonSize))
					rotate_object_z_down(scene, dt);
			}

			igEndTabItem();
		}
		if (igBeginTabItem("Actions", p_open, NULL)) {
			// generating entities
			{
				// create a new object at (0, 0, 0)
				ImVec2 buttonSize;
				buttonSize.x = 0;
				buttonSize.y = 0;
				if (igButton("Generate new object", buttonSize))
					scene->current_entity = add_object_to_scene(scene);
			}
			igEndTabItem();
		}
		igEndTabBar();
	}

	igEnd();
}

// ===========================================================================================
//                                   COMPONENT ADD/REPLACE/ETC
// ===========================================================================================

static void replace_name(scene_t* scene, ecs_entity_ref_t entity, const char* new_name) {
	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, entity, scene->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), new_name);
}

static void add_collider(scene_t* scene, ecs_entity_ref_t entity) {
	add_entity_type_to_object(scene, entity, scene->collider_type);
	collider_component_t* col_comp = ecs_entity_get_component(scene->ecs, entity, scene->collider_type, true);
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, entity, scene->transform_type, true);

	col_comp->transform = transform_comp->transform;
	col_comp->component_size = (vec3f_t){ .x = 1.0f, .y = 1.0f, .z = 1.0f };
}

// ===========================================================================================
//                                           OBJECTS
// ===========================================================================================

static void load_object_scene_resources(scene_t* scene) {
	scene->vertex_shader_work = fs_read(scene->fs, "shaders/triangle-vert.spv", scene->heap, false, false);
	scene->fragment_shader_work = fs_read(scene->fs, "shaders/triangle-frag.spv", scene->heap, false, false);
	scene->object_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(scene->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(scene->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(scene->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(scene->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
	};
	static uint16_t cube_indices[] =
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

	scene->object_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

// add a simple blank object to the scene
static ecs_entity_ref_t add_object_to_scene(scene_t* scene) {
	uint64_t k_object_ent_mask =
		(1ULL << scene->transform_type) |
		(1ULL << scene->name_type) |
		(1ULL << scene->model_type);
	scene->all_ent[scene->next_free_entity] = ecs_entity_add(scene->ecs, k_object_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->transform_type, true);
	transform_identity(&transform_comp->transform);

	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->name_type, true);
	const char* name = heap_alloc(scene->heap, sizeof(char), 1);

	snprintf(name, 10 + sizeof(int), "object_%d", scene->next_free_entity);
	strcpy_s(name_comp->name, sizeof(name_comp->name), name);

	// heap_free(scene->heap, name);

	model_component_t* model_comp = ecs_entity_get_component(scene->ecs, scene->all_ent[scene->next_free_entity], scene->model_type, true);
	model_comp->mesh_info = &scene->object_mesh;
	model_comp->shader_info = &scene->object_shader;

	update_next_entity_location(scene);

	return scene->all_ent[scene->next_free_entity];
}

// add an entity type to an existing entity (given an entity and the new added entity type)
static void add_entity_type_to_object(scene_t* scene, ecs_entity_ref_t entity, int entity_type) {
	uint64_t add_ent_mask = (1ULL << entity_type);
	ecs_add_component_mask(scene->ecs, entity, add_ent_mask);
}

static void load_object_resources(scene_t* scene)
{
	scene->vertex_shader_work = fs_read(scene->fs, "shaders/default.vert.spv", scene->heap, false, false);
	scene->fragment_shader_work = fs_read(scene->fs, "shaders/default.frag.spv", scene->heap, false, false);
	scene->object_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(scene->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(scene->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(scene->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(scene->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f,  0.0f },
		{ 1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ 1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{ 1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{ 1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
	};
	static uint16_t cube_indices[] =
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

	scene->cube_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

// unload all the shader resources used for the frag/vertex shader work
static void unload_shader_resources(scene_t* scene)
{
	fs_work_destroy(scene->fragment_shader_work);
	fs_work_destroy(scene->vertex_shader_work);
}

// moving and rotating (x, y, z) due to the current "selected" entity
static void move_object_x_up(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void move_object_x_down(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void move_object_y_up(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void move_object_y_down(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void move_object_z_up(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_forward(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void move_object_z_down(scene_t* scene, float dt) {
	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_forward(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_x_up(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_up(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_x_down(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_up(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_y_up(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_right(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_y_down(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_right(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_z_up(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_forward(), -dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void rotate_object_z_down(scene_t* scene, float dt) {

	transform_component_t* transform_comp = ecs_entity_get_component(scene->ecs, scene->current_entity, scene->transform_type, true);

	transform_t move;
	transform_identity(&move);

	move.rotation = quatf_from_eulers(vec3f_scale(vec3f_forward(), dt));

	transform_multiply(&transform_comp->transform, &move);
}

static void scene_interaction(scene_t* scene)
{
	float dt = (float)timer_object_get_delta_ms(scene->timer) * 0.001f;

	dt *= 1.0f;

	uint32_t key_mask = wm_get_key_mask(scene->window);

	if (key_mask & k_key_zero & k_key_nine) { // reset dummy
		scene->current_entity = dummy_entity;
	}

	if (key_mask & k_key_zero && ecs_entity_is_dummy_entity(scene->current_entity)) { // add object to the scene
		scene->current_entity = add_object_to_scene(scene);
	}

	if (key_mask & k_key_nine && ecs_entity_is_dummy_entity(scene->current_entity)) { // select camera a the current entity
		scene->current_entity = scene->camera_ent;
	}

	/*
	*	USED FOR MOVING THE CURRENT SELECTED ENTITY
	*	(ARROW KEYS TO MOVE IN THOSE DIRECTIONS)
	*/
	if(!ecs_entity_is_dummy_entity(scene->current_entity)) {
		if (key_mask & k_key_one) {
			if (key_mask & k_key_up) {
				move_object_x_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				move_object_x_down(scene, dt);
			}
		}
		if (key_mask & k_key_two) {
			if (key_mask & k_key_up) {
				move_object_y_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				move_object_y_down(scene, dt);
			}
		}
		if (key_mask & k_key_three) {
			if (key_mask & k_key_up) {
				move_object_z_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				move_object_z_down(scene, dt);
			}
		}

		if (key_mask & k_key_four) {
			if (key_mask & k_key_up) {
				rotate_object_x_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				rotate_object_x_down(scene, dt);
			}
		}

		if (key_mask & k_key_five) {
			if (key_mask & k_key_up) {
				rotate_object_y_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				rotate_object_y_up(scene, dt);
			}
		}

		if (key_mask & k_key_six) {
			if (key_mask & k_key_up) {
				rotate_object_z_up(scene, dt);
			}
			if (key_mask & k_key_down) {
				rotate_object_z_down(scene, dt);
			}
		}
	}
}

// ===========================================================================================
//                                           BOUNDARY
// ===========================================================================================

static void create_boundaries(boundary_t* boundary, float x_pos, float y_pos, float x_neg, float y_neg, float z_pos, float z_neg) {
	boundary->x_pos = x_pos;
	boundary->y_pos = y_pos;
	boundary->x_neg = x_neg;
	boundary->y_neg = y_neg;
	boundary->z_pos = z_pos;
	boundary->z_neg = z_neg;
}

static bool in_boundary_pos_x(boundary_t boundary, transform_t transform) {
	return boundary.x_pos > transform.translation.x;
}

static bool in_boundary_neg_x(boundary_t boundary, transform_t transform) {
	return boundary.x_neg < transform.translation.x;
}

static bool in_boundary_pos_y(boundary_t boundary, transform_t transform) {
	return boundary.y_pos > transform.translation.y;
}

static bool in_boundary_neg_y(boundary_t boundary, transform_t transform) {
	return boundary.y_neg < transform.translation.y;
}

static bool in_boundary_pos_z(boundary_t boundary, transform_t transform) {
	return boundary.z_pos > transform.translation.z;
}

static bool in_boundary_neg_z(boundary_t boundary, transform_t transform) {
	return boundary.z_neg < transform.translation.z;
}

static bool in_boundary(boundary_t boundary, transform_t transform) {
	return (in_boundary_pos_x(boundary, transform) && in_boundary_neg_x(boundary, transform)) &&
		(in_boundary_pos_y(boundary, transform) && in_boundary_neg_y(boundary, transform)) &&
		(in_boundary_pos_z(boundary, transform) && in_boundary_neg_z(boundary, transform));
}

// ===========================================================================================
//                                           COLLIDERS
// ===========================================================================================

// get the minimum point of the collision aligned bounding box
static vec3f_t get_collision_min(collider_component_t col) {
	return (vec3f_t) {
		.x = min(col.transform.translation.x + col.component_size.x, col.transform.translation.x - col.component_size.x),
			.y = min(col.transform.translation.y + col.component_size.y, col.transform.translation.y - col.component_size.y),
			.z = min(col.transform.translation.z + col.component_size.z, col.transform.translation.z - col.component_size.z),
	};
}

// get the maximum point of the collision aligned bounding box
static vec3f_t get_collision_max(collider_component_t col) {
	return (vec3f_t) {
		.x = max(col.transform.translation.x + col.component_size.x, col.transform.translation.x - col.component_size.x),
			.y = max(col.transform.translation.y + col.component_size.y, col.transform.translation.y - col.component_size.y),
			.z = max(col.transform.translation.z + col.component_size.z, col.transform.translation.z - col.component_size.z),
	};
}

// from the minimum and maximum collision points, determine if there is a collision between two colliders
static bool check_collision(collider_component_t one, collider_component_t two) {
	vec3f_t one_min = get_collision_min(one);
	vec3f_t two_min = get_collision_min(two);
	vec3f_t one_max = get_collision_max(one);
	vec3f_t two_max = get_collision_max(two);

	return ((one_min.x <= two_max.x) && (one_max.x >= two_min.x) &&
		(one_min.y <= two_max.y) && (one_max.y >= two_min.y) &&
		(one_min.z <= two_max.z) && (one_max.z >= two_min.z));
}

