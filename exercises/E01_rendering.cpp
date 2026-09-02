#include "SDL3/SDL_init.h"
#define STB_IMAGE_IMPLEMENTATION

#define ENABLE_DIAGNOSTICS

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <iostream>


// simple macro to determine if an SDL call was successful, and print diagnostics if there was a problem
// usage:
// - normal calls      : SDL_VALIDATE(SDL_SetRenderScale(context.renderer, zoom, zoom));
// - with return value : SDL_VALIDATE(window = SDL_CreateWindow(title, window_w, window_h, 0));
// NOTE: in the second case, remember that you can't declare and assign the variable at the same time!
//       SDL_Window* window; // variable declaration
//       SDL_VALIDATE(window = SDL_CreateWindow(title, window_w, window_h, 0)); // variable assignment + test
//       SDL_VALIDATE(SDL_Window* window2 = SDL_CreateWindow(title, window_w, window_h, 0)); // declaration + assignment (Error: expected expression)
#define SDL_VALIDATE(expression) if(!(expression)) { SDL_Log("%s\n", SDL_GetError()); }

#define NANOS(x)   (x)                // converts nanoseconds into nanoseconds
#define MICROS(x)  (NANOS(x) * 1000)  // converts microseconds into nanoseconds
#define MILLIS(x)  (MICROS(x) * 1000) // converts milliseconds into nanoseconds
#define SECONDS(x) (MILLIS(x) * 1000) // converts seconds into nanoseconds

#define NS_TO_MILLIS(x)  ((float)(x)/(float)1000000)    // converts nanoseconds to milliseconds (in floating point precision)
#define NS_TO_SECONDS(x) ((float)(x)/(float)1000000000) // converts nanoseconds to seconds (in floating point precision)

const int NUM_ASTEROIDS = 10;
const int NUM_PROJECTILES = 20;
const int PROJECTILE_OFFSET = 20;
const int DEBUG_CIRCLE_POINT_COUNT = 8;
const float TAU = 6.2831f; // PI*2

struct E01_EngineContext
{
	SDL_Renderer* renderer;
	float window_w; // current window width after render zoom has been applied
	float window_h; // current window height after render zoom has been applied

	float delta;    // in seconds

	bool btn_pressed_up    = false;
	bool btn_pressed_down  = false;
	bool btn_pressed_left  = false;
	bool btn_pressed_right = false;
	bool btn_pressed_space = false;
};

struct E01_Entity
{
	SDL_FPoint   position;
	float        size;
	float        velocity;

	SDL_FRect    rect;
	SDL_Texture* texture_atlas;
	SDL_FRect    texture_rect;
};

struct E01_DesignParams
{
	float entity_size_world   = 64;
	float entity_size_texture = 128;
	float player_speed = entity_size_world * 5;
	int   player_sprite_coords_x = 4;
	int   player_sprite_coords_y = 0;
	float asteroid_speed_min   = entity_size_world * 2;
	float asteroid_speed_range = entity_size_world * 4;
	int   asteroid_sprite_coords_x = 0;
	int   asteroid_sprite_coords_y = 4;
	float project_speed = entity_size_world * 3;
	int	  projectile_sprite_coords_x = 4;
	int	  projectile_sprite_coords_y = 3;
};

struct E01_GameState
{
	E01_Entity player;
	E01_Entity asteroids[NUM_ASTEROIDS];
	E01_Entity projectiles[NUM_PROJECTILES];
	bool hasShot;

	SDL_Texture* texture_atlas;
};

void         draw_circle(E01_EngineContext* context, float x, float y, float radius);

static float distance_between(SDL_FPoint a, SDL_FPoint b);
static float distance_between_sq(SDL_FPoint a, SDL_FPoint b);

static void init(E01_EngineContext* context, E01_DesignParams* params, E01_GameState* game_state);
static void update(E01_EngineContext* context, E01_DesignParams* params, E01_GameState* game_state);

int main(void)
{
	// NOTE: we are zero-ing out the memory of `game_state` and `context` because it gets filled appropriately later,
	//       but the entire purpose of `design_params` is to supply default values, so we shouldn't do that
	E01_EngineContext context = { 0 };
	E01_GameState game_state  = { 0 };
	E01_DesignParams design_params;

	float window_w = 600;
	float window_h = 800;
	int target_framerate = SECONDS(1) / 60;

	SDL_Window* window = SDL_CreateWindow("E01 - Rendering", window_w, window_h, 0);
	context.renderer = SDL_CreateRenderer(window, NULL);
	context.window_w = window_w;
	context.window_h = window_h;

	// increase the zoom to make debug text more legible
	// (ie, on the class projector, we will usually use 2)
	{
		float zoom = 1;
		context.window_w /= zoom;
		context.window_h /= zoom;
		SDL_SetRenderScale(context.renderer, zoom, zoom);
	}

	bool quit = false;

	SDL_Time walltime_frame_beg;
	SDL_Time walltime_work_end;
	SDL_Time walltime_frame_end;
	SDL_Time time_elapsed_frame;
	SDL_Time time_elapsed_work;

	init(&context, &design_params, &game_state);

	SDL_GetCurrentTime(&walltime_frame_beg);
	while(!quit)
	{
		// input
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_EVENT_QUIT:
					quit = true;
					break;

				case SDL_EVENT_KEY_UP:
					if(event.key.key == SDLK_W)
						context.btn_pressed_up = event.key.down;
					if(event.key.key == SDLK_A)
						context.btn_pressed_left = event.key.down;
					if(event.key.key == SDLK_S)
						context.btn_pressed_down = event.key.down;
					if(event.key.key == SDLK_D)
						context.btn_pressed_right = event.key.down;
					if(event.key.key == SDLK_SPACE) {
						game_state.hasShot = false;
						context.btn_pressed_space = event.key.down;
					}
					break;
				case SDL_EVENT_KEY_DOWN:
					if(event.key.key == SDLK_W)
						context.btn_pressed_up = event.key.down;
					if(event.key.key == SDLK_A)
						context.btn_pressed_left = event.key.down;
					if(event.key.key == SDLK_S)
						context.btn_pressed_down = event.key.down;
					if(event.key.key == SDLK_D)
						context.btn_pressed_right = event.key.down;
					if(event.key.key == SDLK_SPACE) {
						game_state.hasShot = event.key.down;
						context.btn_pressed_space = event.key.down;
					}
			}
		}

		// clear screen
		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x00);
		SDL_RenderClear(context.renderer);

		update(&context, &design_params, &game_state);

		SDL_GetCurrentTime(&walltime_work_end);
		time_elapsed_work = walltime_work_end - walltime_frame_beg;

		if(target_framerate > time_elapsed_work)
		{
			SDL_DelayPrecise(target_framerate - time_elapsed_work);
		}

		SDL_GetCurrentTime(&walltime_frame_end);
		time_elapsed_frame = walltime_frame_end - walltime_frame_beg;

		context.delta = NS_TO_SECONDS(time_elapsed_frame);

#ifdef ENABLE_DIAGNOSTICS
		{
			// draw semi-transparent background
			const SDL_FRect rect = SDL_FRect{ 5, 5, 305, 35 };
			SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x99);
			SDL_RenderFillRect(context.renderer, &rect);

			// draw statistics text
			SDL_SetRenderDrawColor(context.renderer, 0xFF, 0xFF, 0xFF, 0xFF);
			SDL_RenderDebugTextFormat(context.renderer, 10.0f, 10.0f, "elapsed (frame): %9.6f ms", NS_TO_MILLIS(time_elapsed_frame));
			SDL_RenderDebugTextFormat(context.renderer, 10.0f, 20.0f, "elapsed(work)  : %9.6f ms", NS_TO_MILLIS(time_elapsed_work));
		}
#endif

		// render
		SDL_RenderPresent(context.renderer);

		walltime_frame_beg = walltime_frame_end;
	}

	SDL_Quit();
	
	return 0;
};

// simple function to draw debug circles
// (you might find it usefult to debug your collisions)
//
// NOTE: this function is very slow (it does a lot of trig. for instance)
//       there are many ways to make this faster, any ideas?
void draw_circle(E01_EngineContext* context, float x, float y, float radius)
{
	SDL_FPoint points[DEBUG_CIRCLE_POINT_COUNT+1];

	float angle_increment = TAU / DEBUG_CIRCLE_POINT_COUNT;
	for(int i = 0; i < DEBUG_CIRCLE_POINT_COUNT; ++i)
	{
		float angle = angle_increment * i;
		points[i].x = x + radius * SDL_cos(angle);
		points[i].y = y + radius * SDL_sin(angle);
	}
	points[DEBUG_CIRCLE_POINT_COUNT] = points[0];
	SDL_SetRenderDrawColor(context->renderer, 0x00, 0xFF, 0x00, 0xFF);
	SDL_RenderLines(context->renderer, points, DEBUG_CIRCLE_POINT_COUNT+1);
}

static float distance_between(SDL_FPoint a, SDL_FPoint b)
{
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	return SDL_sqrtf(dx*dx + dy*dy);
}

static float distance_between_sq(SDL_FPoint a, SDL_FPoint b)
{
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	return dx*dx + dy*dy;
}

static void init(E01_EngineContext* context, E01_DesignParams* params, E01_GameState* game_state)
{
	// load textures
	{
		int w = 0;
		int h = 0;
		int n = 0;
		unsigned char* pixels = stbi_load("data/kenney/simpleSpace_tilesheet_2.png", &w, &h, &n, 0);

		SDL_assert(pixels);

		// we don't really need this SDL_Surface, but it's the most conveninet way to create out texture
		// NOTE: out image has the color channels in RGBA order, but SDL_PIXELFORMAT
		//       behaves the opposite on little endian architectures (ie, most of them)
		//       we won't worry too much about that, just remember this if your textures looks wrong
		//       - check that the the color channels are actually what you expect (how many? how big? which order?)
		//       - if everythig looks right, you might just need to flip the channel order, because of SDL
		SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, pixels, w * n);
		game_state->texture_atlas = SDL_CreateTextureFromSurface(context->renderer, surface);

		// NOTE: the texture will make a copy of the pixel data, so after creation we can release both surface and pixel data
		SDL_DestroySurface(surface);
		stbi_image_free(pixels);
	}

	// character
	{
		game_state->player.position.x = context->window_w / 2 - params->entity_size_world / 2;
		game_state->player.position.y = context->window_h - params->entity_size_world * 2;
		game_state->player.size = params->entity_size_world;
		game_state->player.velocity = params->player_speed;
		game_state->player.texture_atlas = game_state->texture_atlas;

		// player size in the game world
		game_state->player.rect.w = game_state->player.size;
		game_state->player.rect.h = game_state->player.size;

		// sprite size (in the tilemap)
		game_state->player.texture_rect.w = params->entity_size_texture;
		game_state->player.texture_rect.h = params->entity_size_texture;
		// sprite position (in the tilemap)
		game_state->player.texture_rect.x = params->entity_size_texture * params->player_sprite_coords_x;
		game_state->player.texture_rect.y = params->entity_size_texture * params->player_sprite_coords_y;
	}

	// asteroids
	{
		for(int i = 0; i < NUM_ASTEROIDS; ++i)
		{
			E01_Entity* asteroid_curr = &game_state->asteroids[i];

			asteroid_curr->position.x = params->entity_size_world + SDL_randf() * (context->window_w - params->entity_size_world * 2);
			asteroid_curr->position.y = -params->entity_size_world; // spawn asteroids off screen (almost)
			asteroid_curr->size       = params->entity_size_world;
			asteroid_curr->velocity   = params->asteroid_speed_min + SDL_randf() * params->asteroid_speed_range;
			asteroid_curr->texture_atlas = game_state->texture_atlas;

			asteroid_curr->rect.w = asteroid_curr->size;
			asteroid_curr->rect.h = asteroid_curr->size;

			asteroid_curr->texture_rect.w = params->entity_size_texture;
			asteroid_curr->texture_rect.h = params->entity_size_texture;

			asteroid_curr->texture_rect.x = params->entity_size_texture * params->asteroid_sprite_coords_x;
			asteroid_curr->texture_rect.y = params->entity_size_texture * params->asteroid_sprite_coords_y;
		}
	}

	// projectiles
	{
		for(int i = 0; i < NUM_PROJECTILES; ++i)
		{
			E01_Entity* projectile_curr = &game_state->projectiles[i];

			projectile_curr->texture_atlas = game_state->texture_atlas;
			projectile_curr->size = params->entity_size_world;
			projectile_curr->position.x = -params->entity_size_world - PROJECTILE_OFFSET;
			projectile_curr->position.y = 0;
			projectile_curr->velocity = params->project_speed;
			projectile_curr->rect.w = projectile_curr->size;
			projectile_curr->rect.h = projectile_curr->size;

			projectile_curr->texture_rect.w = params->entity_size_texture;
			projectile_curr->texture_rect.h = params->entity_size_texture;
			projectile_curr->texture_rect.x = params->entity_size_texture * params->projectile_sprite_coords_x;
			projectile_curr->texture_rect.y = params->entity_size_texture * params->projectile_sprite_coords_y;
		}
	}
}

static bool isProjectileActive(E01_Entity* projectile) {
	return projectile->position.x >= 0;
}

static void createProjectile(E01_DesignParams* params, E01_GameState* game_state) {
	int projectile_index = -1;
	for(int i = 0; i < NUM_PROJECTILES; ++i)
	{
		if (!isProjectileActive(&game_state->projectiles[i])) 
		{
			projectile_index = i;
			break;
		}
	}
	if (projectile_index == -1) return;
	printf("%d", projectile_index);
	
	E01_Entity* projectile_curr = &game_state->projectiles[projectile_index];	
	projectile_curr->position.x = game_state->player.position.x;
	projectile_curr->position.y = game_state->player.position.y;
}

static void update(E01_EngineContext* context, E01_DesignParams* params, E01_GameState* game_state)
{
	// player
	{
		E01_Entity* entity_player = &game_state->player;
		if(context->btn_pressed_up)
			entity_player->position.y -= context->delta * entity_player->velocity;
		if(context->btn_pressed_down)
			entity_player->position.y += context->delta * entity_player->velocity;
		if(context->btn_pressed_left)
			entity_player->position.x -= context->delta * entity_player->velocity;
		if(context->btn_pressed_right)
			entity_player->position.x += context->delta * entity_player->velocity;
		if(context->btn_pressed_space) {
			createProjectile(params, game_state);
		}
			
		

		entity_player->rect.x = entity_player->position.x;
		entity_player->rect.y = entity_player->position.y;
		SDL_SetTextureColorMod(entity_player->texture_atlas, 0xFF, 0xFF, 0xFF);
		SDL_RenderTexture(
			context->renderer,
			entity_player->texture_atlas,
			&entity_player->texture_rect,
			&entity_player->rect
		);
	}

	// asteroids
	{
		// how close an asteroid must be before categorizing it as "too close" (100 pixels. We square it because we can avoid doing the square root later)
		const float warning_distance_sq = 100*100;

		// how close an asteroid must be before triggering a collision (64 pixels. We square it because we can avoid doing the square root later)
		// the number 64 is obtained by summing togheter the "radii" of the sprites
		const float collision_distance_sq = 64*64;

		for(int i = 0; i < NUM_ASTEROIDS; ++i)
		{
			E01_Entity* asteroid_curr = &game_state->asteroids[i];
			asteroid_curr->position.y += context->delta * asteroid_curr->velocity;

			asteroid_curr->rect.x = asteroid_curr->position.x;
			asteroid_curr->rect.y = asteroid_curr->position.y;

			float distance_sq = distance_between_sq(asteroid_curr->position, game_state->player.position);
			if(distance_sq < collision_distance_sq)
				SDL_SetTextureColorMod(asteroid_curr->texture_atlas, 0xFF, 0x00, 0x00);
			else if(distance_sq < warning_distance_sq)
				SDL_SetTextureColorMod(asteroid_curr->texture_atlas, 0xCC, 0xCC, 0x00);
			else
				SDL_SetTextureColorMod(asteroid_curr->texture_atlas, 0xFF, 0xFF, 0xFF);

			SDL_RenderTexture(
				context->renderer,
				asteroid_curr->texture_atlas,
				&asteroid_curr->texture_rect,
				&asteroid_curr->rect
			);
		}
	}

	// projectiles
	{
		const float collision_distance_sq = 64*64;

		for(int i = 0; i < NUM_PROJECTILES; ++i)
		{
			
			E01_Entity* projectile_curr = &game_state->projectiles[i];
			if (!isProjectileActive(projectile_curr)) continue;
			projectile_curr->position.y -= context->delta * projectile_curr->velocity;

			projectile_curr->rect.x = projectile_curr->position.x;
			projectile_curr->rect.y = projectile_curr->position.y;

			/*
			Don't think we need to recolor the projectiles
			float distance_sq = distance_between_sq(projectile_curr->position, game_state->player.position);
			if(distance_sq < collision_distance_sq)
				SDL_SetTextureColorMod(projectile_curr->texture_atlas, 0xFF, 0x00, 0x00);
			else
				SDL_SetTextureColorMod(projectile_curr->texture_atlas, 0xFF, 0xFF, 0xFF);
			*/
			
			SDL_RenderTexture(
				context->renderer,
				projectile_curr->texture_atlas,
				&projectile_curr->texture_rect,
				&projectile_curr->rect
			);
		}
		 
	}
}
