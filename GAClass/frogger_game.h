#pragma once

// Frogger Game
/*
	Traffic moves in lanes horizontally across the screen.

	Player character moves vertically from bottom of the screen to the top.

	There should be at least 3 lanes of traffic with 
	space between each lane for a player character to wait.

	If the player succesfully reaches the top of the screen, 
	they are respawned at the bottom.

	If the player touches traffic, they are respawned at the bottom of the screen.

	The player character moves in response to arrow keys.

	Traffic is composed of rectangles of variable length. 
	The player is a square (or cube).

	The player should be green. Traffic can be arbitrary colors.

	The game should use an orthographic projection matrix for the 
	camera (not a perspective projection).
*/

typedef struct frogger_game_t frogger_game_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of simple test game.
frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render);

// Destroy an instance of simple test game.
void frogger_game_destroy(frogger_game_t* game);

// Per-frame update for our simple test game.
void frogger_game_update(frogger_game_t* game);