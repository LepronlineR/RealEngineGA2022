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

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

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

typedef enum lane_direction {
	right_lane, left_lane
} lane_direction;

typedef struct enemy_component_t {
	lane_direction direction;
	boundary_t boundary;
	int lane;
} enemy_component_t;

typedef struct win_component_t {
	boundary_t boundary;
	int total_wins;
	transform_t respawn_location;
} win_component_t;

//typedef struct collider_component_t {

//};

//typedef struct gpu_info_list_t {
//	gpu_mesh_info_t mesh;
//	gpu_shader_info_t shader;
//	gpu_info_list_t* next;
//} gpu_info_list_t;

typedef struct frogger_game_t
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
	ecs_entity_ref_t enemy_ent;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t player_mesh;
	gpu_shader_info_t player_shader;

	gpu_mesh_info_t enemy_mesh;
	gpu_shader_info_t enemy_shader;

	// gpu_info_list_t enemies;

	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_player_resources(frogger_game_t* game);
static void unload_player_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void draw_models(frogger_game_t* game);


// boundary
static bool in_boundary(boundary_t boundary, transform_t transform);
static void create_boundaries(boundary_t* boundary, float x_pos, float y_pos, float x_neg, float y_neg, float z_pos, float z_neg);

// enemies
static void load_enemy_resources(frogger_game_t* game);
static void spawn_car(frogger_game_t* game);
static void update_enemies(frogger_game_t* game);

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	game->enemy_type = ecs_register_component_type(game->ecs, "enemy", sizeof(enemy_component_t), _Alignof(enemy_component_t));
	game->win_type = ecs_register_component_type(game->ecs, "win", sizeof(win_component_t), _Alignof(win_component_t));

	load_player_resources(game);
	load_enemy_resources(game, 2);
	spawn_player(game, 0);
	spawn_camera(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_player_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game) {
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_players(game);

	// spawn enemies on every randomized 3 to 6 seconds
	if(timer_object_get_delta_ms(game->timer) % 100 * (random_i(100, 200)))
		spawn_car(game);

	update_enemies(game);

	draw_models(game);
	render_push_done(game->render);
}

static void create_boundaries(boundary_t* boundary, float x_pos, float y_pos, float x_neg, float y_neg, float z_pos, float z_neg) {
	boundary->x_pos = x_pos;
	boundary->y_pos = y_pos;
	boundary->x_neg = x_neg;
	boundary->y_neg = y_neg;
	boundary->z_pos = z_pos;
	boundary->z_neg = z_neg;
}

static void load_player_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->player_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -0.5f, -0.5f, 0.5f }, { 0.0f, 1.0f,  0.0f },
		{ 0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f,  0.0f },
		{ 0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f,  0.0f },
		{ -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f,  0.0f },
		{ -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f,  0.0f },
		{ 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f,  0.0f },
		{ 0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f,  0.0f },
		{ -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f,  0.0f },
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

static void load_enemy_resources(frogger_game_t* game) {
	game->enemy_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -0.5f, -1.0f,  0.5f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f, -1.0f,  0.5f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f,  1.0f,  0.5f }, { 1.0f, 0.0f,  0.0f },
		{ -0.5f,  1.0f,  0.5f }, { 1.0f, 0.0f,  0.0f },
		{ -0.5f, -1.0f, -0.5f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f, -1.0f, -0.5f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f,  1.0f, -0.5f }, { 1.0f, 0.0f,  0.0f },
		{ -0.5f,  1.0f, -0.5f }, { 1.0f, 0.0f,  0.0f },
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
	game->enemy_mesh = (gpu_mesh_info_t)
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
		(1ULL << game->win_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = -5.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	create_boundaries(&player_comp->boundary, 6.0f, 6.0f, -6.0f, -6.0f, 6.0f, -6.0f);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->player_shader;

	win_component_t* win_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->win_type, true);
	create_boundaries(&win_comp->boundary, 99.0f, 99.0f, -99.0f, -99.0f, 99.0f, -5.0f);
	win_comp->total_wins = 0;
	transform_t respawn_loc;
	transform_identity(&respawn_loc);
	respawn_loc.translation.z = 5.0f;
	win_comp->respawn_location = respawn_loc;
}

static void spawn_car(frogger_game_t* game) {
	uint64_t k_enemy_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->enemy_type) |
		(1ULL << game->name_type);
	game->enemy_ent = ecs_entity_add(game->ecs, k_enemy_ent_mask);

	// choose random lane
	int lane = random_i(0, 2);
	lane_direction direction = (lane % 2 == 0 ? right_lane : left_lane);
	float starting_area = (direction == right_lane ? -11.0f : 11.0f);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.y = starting_area;
	transform_comp->transform.translation.z = lane * 3 - 5;
	transform_comp->transform.translation.x = 0.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "enemy");

	enemy_component_t* enemy_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->enemy_type, true);
	enemy_comp->direction = direction;
	enemy_comp->lane = lane;
	create_boundaries(&enemy_comp->boundary, 12.0f, 12.0f, -12.0f, -12.0f, 12.0f, -12.0f);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->model_type, true);
	model_comp->mesh_info = &game->enemy_mesh;
	model_comp->shader_info = &game->enemy_shader;
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static bool in_boundary(boundary_t boundary, transform_t transform) {
	if (boundary.x_pos < transform.translation.x || boundary.x_neg > transform.translation.x ||
		boundary.y_pos < transform.translation.y || boundary.y_neg > transform.translation.y ||
		boundary.z_pos < transform.translation.z || boundary.z_neg > transform.translation.z)
		return false;
	return true;
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

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
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
		}
		if (key_mask & k_key_down)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
		}
		if (key_mask & k_key_left)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}
		if (key_mask & k_key_right)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		}

		// check if out of boundaries
		transform_component_t temp = *transform_comp;
		transform_multiply(&temp, &move);

		if(in_boundary(player_comp->boundary, temp.transform))
			transform_multiply(&transform_comp->transform, &move);

		// win condition if the player is outside of the winning boundary
		if (!in_boundary(win_comp->boundary, transform_comp->transform)) {
			// respawn the player
			win_comp->total_wins += 1;
			transform_comp->transform = win_comp->respawn_location;
		}
	}
}

static void update_enemies(frogger_game_t* game) {
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->enemy_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query)){
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		enemy_component_t* enemy_comp = ecs_query_get_component(game->ecs, &query, game->enemy_type);

		transform_t move;
		transform_identity(&move);

		if (enemy_comp->direction == right_lane) { // right direction
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		} else { // left direction
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}

		transform_multiply(&transform_comp->transform, &move);

		// remove the enemy if it is out of the boundary
		if (!in_boundary(enemy_comp->boundary, transform_comp->transform)) {
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), true);
		}
	}
}

static void draw_models(frogger_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

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

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}