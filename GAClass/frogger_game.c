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
#include <stdint.h>

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

// Axis Aligned Bounding Boxes
typedef struct collider_component_t {
	// Note that we are using transform without a pointer or in its own component, so colliders can have
	// their own behavior when outside of any component that it is supposed to use
	// i.e. player components will have to control the transform of the collider
	transform_t transform;
	float x_size;
	float y_size;
	float z_size;
} collider_component_t;

typedef struct timer_component_t {
	uint32_t time_tracker;
	float interval;
} timer_component_t;

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
	int collider_type;
	int timer_type;

	ecs_entity_ref_t enemy_ent;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t timer_ent;

	gpu_mesh_info_t player_mesh;
	gpu_shader_info_t player_shader;

	gpu_mesh_info_t enemy_mesh;
	gpu_shader_info_t enemy_shader;

	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

// general
static void draw_models(frogger_game_t* game);
static void initialize_timer(frogger_game_t* game, float interval);

// camera
static void spawn_camera(frogger_game_t* game);

// player
static void load_player_resources(frogger_game_t* game);
static void unload_player_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void update_players(frogger_game_t* game);

// boundary
static bool in_boundary(boundary_t boundary, transform_t transform);
static void create_boundaries(boundary_t* boundary, float x_pos, float y_pos, float x_neg, float y_neg, float z_pos, float z_neg);

// colliders
static bool check_collision(collider_component_t one, collider_component_t two);

// enemies
static void spawn_enemy_with_timer(frogger_game_t* game);
static void load_enemy_resources(frogger_game_t* game);
static void spawn_car(frogger_game_t* game);
static void update_enemies(frogger_game_t* game);

// ===========================================================================================
//                                           GENERAL
// ===========================================================================================

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
	game->collider_type = ecs_register_component_type(game->ecs, "collider", sizeof(collider_component_t), _Alignof(collider_component_t));
	game->timer_type = ecs_register_component_type(game->ecs, "timer", sizeof(timer_component_t), _Alignof(timer_component_t));

	load_player_resources(game);
	load_enemy_resources(game, 2);
	initialize_timer(game, 1.5f);
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

	spawn_enemy_with_timer(game);

	update_players(game);
	update_enemies(game);

	draw_models(game);
	render_push_done(game->render);
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

static void initialize_timer(frogger_game_t* game, float interval) {
	uint64_t k_timer_ent_mask =
		(1ULL << game->timer_type) |
		(1ULL << game->name_type);
	game->timer_ent = ecs_entity_add(game->ecs, k_timer_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->timer_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "timer");

	timer_component_t* timer_comp = ecs_entity_get_component(game->ecs, game->timer_ent, game->timer_type, true);
	timer_comp->time_tracker = 0;
	timer_comp->interval = interval;
}

// ===========================================================================================
//                                           CAMERA
// ===========================================================================================

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "orthographic camera");

	// adjust due to aspect ratio
	float aspect_width = 16.0f;
	float aspect_height = 9.0f;
	float aspect_ratio = aspect_width / aspect_height; // aspect ratio w/h

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);

	mat4f_make_orthographic(&camera_comp->projection, -aspect_ratio, aspect_ratio,
		-1.0f, 1.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

// ===========================================================================================
//                                           PLAYER
// ===========================================================================================

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
	transform_comp->transform.translation.z = 5.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	create_boundaries(&player_comp->boundary, 6.0f, 6.0f, -6.0f, -6.0f, 6.0f, -6.0f);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->player_shader;

	collider_component_t* col_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->collider_type, true);
	col_comp->transform = transform_comp->transform;
	col_comp->x_size = 1.0f;
	col_comp->y_size = 1.0f;
	col_comp->z_size = 1.0f;

	win_component_t* win_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->win_type, true);
	create_boundaries(&win_comp->boundary, 99.0f, 99.0f, -99.0f, -99.0f, 99.0f, -5.0f);
	win_comp->total_wins = 0;
	transform_t respawn_loc;
	transform_identity(&respawn_loc);
	respawn_loc.translation.z = 5.0f;
	win_comp->respawn_location = respawn_loc;
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

		if (in_boundary(player_comp->boundary, temp.transform))
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

static bool in_boundary(boundary_t boundary, transform_t transform) {
	if (boundary.x_pos < transform.translation.x || boundary.x_neg > transform.translation.x ||
		boundary.y_pos < transform.translation.y || boundary.y_neg > transform.translation.y ||
		boundary.z_pos < transform.translation.z || boundary.z_neg > transform.translation.z)
		return false;
	return true;
}

// ===========================================================================================
//                                           COLLIDERS
// ===========================================================================================

static bool check_collision(collider_component_t one, collider_component_t two) {
	// collision x
	bool col_x = one.transform.translation.x + one.x_size >= two.transform.translation.x &&
		two.transform.translation.x + two.x_size >= one.transform.translation.x;
	// collision y
	bool col_y = one.transform.translation.y + one.y_size >= two.transform.translation.y &&
		two.transform.translation.y + two.y_size >= one.transform.translation.y;
	// collision z
	bool col_z = one.transform.translation.z + one.z_size >= two.transform.translation.z &&
		two.transform.translation.z + two.z_size >= one.transform.translation.z;

	// collision only if on all axes
	return col_x && col_y && col_z;
}

// ===========================================================================================
//                                           ENEMIES
// ===========================================================================================

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

static void spawn_car(frogger_game_t* game) {
	uint64_t k_enemy_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->enemy_type) |
		(1ULL << game->name_type) |
		(1ULL << game->collider_type);
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

	collider_component_t* col_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->collider_type, true);
	col_comp->transform = transform_comp->transform;
	col_comp->x_size = 1.0f;
	col_comp->y_size = 2.0f;
	col_comp->z_size = 1.0f;
}

static void spawn_enemy_with_timer(frogger_game_t* game) {
	uint64_t k_query_mask = (1ULL << game->name_type) | (1ULL << game->timer_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query)) {
		
		timer_component_t* timer_comp = ecs_query_get_component(game->ecs, &query, game->timer_type);
		timer_comp->time_tracker += timer_object_get_delta_ms(game->timer);

		if ((float) timer_comp->time_tracker/1000 >= timer_comp->interval) { // correct time passed in the interval (spawn enemy)
			timer_comp->time_tracker = 0;
			spawn_car(game);
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

		collider_component_t* col_comp =
			ecs_query_get_component(game->ecs, &query, game->collider_type);

		// update collision
		col_comp->transform = transform_comp->transform;

		// check for player collision
		uint64_t k_query_player_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

		for (ecs_query_t player_query = ecs_query_create(game->ecs, k_query_player_mask);
			ecs_query_is_valid(game->ecs, &player_query);
			ecs_query_next(game->ecs, &player_query)){ // for each player check if there is a collision

			collider_component_t* player_col_comp = 
				ecs_query_get_component(game->ecs, &player_query, game->collider_type);

			if (check_collision(*player_col_comp, *col_comp)) {

				/* UNCOMMENT TO PRINT THE COLLISION WHEN IT HAPPENS
				name_component_t* player_name_comp =
					ecs_query_get_component(game->ecs, &player_query, game->name_type);
				name_component_t* enemy_name_comp =
					ecs_query_get_component(game->ecs, &query, game->name_type);
				debug_print_line(k_print_info, "collided %s with %s\n", player_name_comp->name, enemy_name_comp->name);
				*/

				win_component_t* player_win_comp = ecs_query_get_component(game->ecs, &player_query, game->win_type);
				transform_component_t* player_transform_comp = ecs_query_get_component(game->ecs, &player_query, game->transform_type);

				// respawn the player back at the respawn location at collision
				player_transform_comp->transform = player_win_comp->respawn_location;
			}

		}
	}
}