#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "random.h"
#include "debug.h"
#include "vec3f.h"
#include "scene.h"
#include "cimgui.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

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

typedef struct player_component_t
{
	int index;
	boundary_t boundary;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

// Axis Aligned Bounding Boxes
typedef struct collider_component_t {
	// Note that we are using transform without a pointer or in its own component, so colliders can have
	// their own behavior when outside of any component that it is supposed to use
	// i.e. player components will have to control the transform of the collider
	transform_t transform;
	vec3f_t component_size;
} collider_component_t;

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
	int player_type;
	int name_type;
	int enemy_type;
	int win_type;
	int collider_type;
	int timer_type;

	ecs_entity_ref_t cube_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t ui_mesh;
	gpu_shader_info_t ui_shader;

	gpu_mesh_info_t object_mesh;
	gpu_shader_info_t object_shader;

	gpu_mesh_info_t sceneui_mesh;
	gpu_shader_info_t sceneui_shader;

	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} scene_t;

// general
static void draw_models(scene_t* scene);

// camera
static void spawn_camera(scene_t* scene);

// scene hierarchy
static void load_scene_hierarchy_resources(scene_t* scene);
static void unload_scene_hierarchy_resources(scene_t* scene);
static void spawn_scene_hierarchy(scene_t* scene);
static void update_scene_hierarchy(scene_t* scene);

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

	scene->timer = timer_object_create(heap, NULL);

	scene->ecs = ecs_create(heap);
	scene->transform_type = ecs_register_component_type(scene->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	scene->camera_type = ecs_register_component_type(scene->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	scene->model_type = ecs_register_component_type(scene->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	scene->player_type = ecs_register_component_type(scene->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	scene->name_type = ecs_register_component_type(scene->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	scene->collider_type = ecs_register_component_type(scene->ecs, "collider", sizeof(collider_component_t), _Alignof(collider_component_t));

	load_scene_hierarchy_resources(scene);

	spawn_scene_hierarchy(scene);

	spawn_camera(scene);

	return scene;
}

void scene_destroy(scene_t* scene)
{
	ecs_destroy(scene->ecs);
	timer_object_destroy(scene->timer);

	// unload_player_resources(scene);
	unload_scene_hierarchy_resources(scene);

	heap_free(scene->heap, scene);
}

void scene_update(scene_t* scene) {
	timer_object_update(scene->timer);
	ecs_update(scene->ecs);

	// 

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
	}
}

// ===========================================================================================
//                                           CAMERA
// ===========================================================================================

// generate a camera with an orthographic projection
static void spawn_camera(scene_t* scene)
{
	uint64_t k_camera_ent_mask =
		(1ULL << scene->camera_type) |
		(1ULL << scene->name_type);
	scene->camera_ent = ecs_entity_add(scene->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(scene->ecs, scene->camera_ent, scene->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "orthographic camera");

	// adjust due to aspect ratio
	float aspect_width = 16.0f;
	float aspect_height = 9.0f;
	float aspect_ratio = aspect_width / aspect_height; // aspect ratio w/h

	camera_component_t* camera_comp = ecs_entity_get_component(scene->ecs, scene->camera_ent, scene->camera_type, true);

	mat4f_make_orthographic(&camera_comp->projection, -aspect_ratio, aspect_ratio,
		-1.0f, 1.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -10.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

// ===========================================================================================
//                                       SCENE HIERARCHY
// ===========================================================================================

static void load_scene_hierarchy_resources(scene_t* scene) {
	scene->vertex_shader_work = fs_read(scene->fs, "shaders/ui-vert.spv", scene->heap, false, false);
	scene->fragment_shader_work = fs_read(scene->fs, "shaders/ui-frag.spv", scene->heap, false, false);
	scene->ui_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(scene->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(scene->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(scene->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(scene->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t plane_verts[] =
	{
		{-0.5f, -0.5f}, { 1.0f, 0.0f, 0.0f },
		{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f},
		{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f},
		{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f},
	};

	static uint16_t plane_indices[] =
	{
		0, 1, 2, 2, 3, 0
	};

	scene->ui_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = plane_verts,
		.vertex_data_size = sizeof(plane_verts),
		.index_data = plane_indices,
		.index_data_size = sizeof(plane_indices),
	};
}

static void unload_scene_hierarchy_resources(scene_t* scene)
{
	fs_work_destroy(scene->fragment_shader_work);
	fs_work_destroy(scene->vertex_shader_work);
}

static void spawn_scene_hierarchy(scene_t* scene) {
	// igCreateContext();
	// igGetIO()->ConfigFlags |= ImGuiConfigFlags_;


}

static void update_scene_hierarchy(scene_t* scene) {

}

// ===========================================================================================
//                                           OBJECTS
// ===========================================================================================

/*
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
	game->player_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_player_resources(frogger_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(frogger_game_t* game, int index) {
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type) |
		(1ULL << game->win_type) |
		(1ULL << game->collider_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = 9.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	create_boundaries(&player_comp->boundary, 9.0f, 9.0f, -9.0f, -9.0f, 9.0f, -9.0f);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->player_shader;

	collider_component_t* col_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->collider_type, true);
	col_comp->transform = transform_comp->transform;
	col_comp->component_size = (vec3f_t){ .x = 1.0f, .y = 1.0f, .z = 1.0f };

	win_component_t* win_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->win_type, true);
	create_boundaries(&win_comp->boundary, 99.0f, 99.0f, -99.0f, -99.0f, 99.0f, -9.0f);
	win_comp->total_wins = 0;
	transform_t respawn_loc;
	transform_identity(&respawn_loc);
	respawn_loc.translation.z = 9.0f;
	win_comp->respawn_location = respawn_loc;
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	dt *= 2.0f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);
		win_component_t* win_comp = ecs_query_get_component(game->ecs, &query, game->win_type);

		if (player_comp->index && transform_comp->transform.translation.z > 1.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up && in_boundary_neg_z(player_comp->boundary, transform_comp->transform))
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
		}
		if (key_mask & k_key_down && in_boundary_pos_z(player_comp->boundary, transform_comp->transform))
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
		}
		if (key_mask & k_key_left && in_boundary_neg_y(player_comp->boundary, transform_comp->transform))
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}
		if (key_mask & k_key_right && in_boundary_pos_y(player_comp->boundary, transform_comp->transform))
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		}

		transform_multiply(&transform_comp->transform, &move);

		// win condition if the player is outside of the winning boundary
		if (!in_boundary(win_comp->boundary, transform_comp->transform)) {
			// respawn the player
			win_comp->total_wins += 1;
			transform_comp->transform = win_comp->respawn_location;
		}

		// update collider transform
		collider_component_t* col_comp = ecs_query_get_component(game->ecs, &query, game->collider_type);
		col_comp->transform = transform_comp->transform;
	}
}
*/

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