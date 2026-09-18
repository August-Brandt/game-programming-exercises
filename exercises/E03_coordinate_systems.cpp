#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>

bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = true;
const int ENTITY_COUNT = 4096;
const int MAP_WIDTH = 16;
const int MAP_HEIGHT = 16;
const int TILESHEET_WIDTH = 12;
const int TILESHEET_HEIGHT = 5;

struct E03_Entity
{
	Sprite sprite;
	Transform2D transform;
};

struct E03_TileMap
{
	Sprite tileset[TILESHEET_WIDTH * TILESHEET_HEIGHT];
	u_int8_t pattern[MAP_WIDTH][MAP_HEIGHT];
	vec2f pos;
};

struct E03_GameState
{
	// shortcut references
	E03_Entity* player;

	// game-allocated memory
	E03_Entity* entities;
	int entities_alive_count;
	E03_TileMap* tilemap;

	// SDL-allocated structures
	SDL_Texture* atlas;
	// SDL_Texture* bg;
};

static E03_Entity* entity_create(E03_GameState* state)
{
	if(!(state->entities_alive_count < ENTITY_COUNT))
		// NOTE: this might as well be an assert, if we don't have a way to recover/handle it
		return NULL;

	// // concise version
	//return &state->entities[state->entities_alive_count++];

	E03_Entity* ret = &state->entities[state->entities_alive_count];
	++state->entities_alive_count;
	return ret;
}

// NOTE: this only works if nobody holds references to other entities!
//       if that were the case, we couldn't swap them around.
//       We will see in later lectures how to handle this kind of problems
static void entity_destroy(E03_GameState* state, E03_Entity* entity)
{
	// NOTE: here we want to fail hard, nobody should pass us a pointer not gotten from `entity_create()`
	SDL_assert(entity < state->entities ||entity > state->entities + ENTITY_COUNT);

	--state->entities_alive_count;
	*entity = state->entities[state->entities_alive_count];
}

static void game_init(EngineContext* context, E03_GameState* state)
{
	// allocate memory
	state->entities = (E03_Entity*)SDL_calloc(ENTITY_COUNT, sizeof(E03_Entity));
	SDL_assert(state->entities);

	// TODO allocate space for tile info (when we'll load those from file)
	state->tilemap = (E03_TileMap*)SDL_calloc(1, sizeof(E03_TileMap));

	// texture atlases
	state->atlas = itu_resources_texture_create(context, "data/kenney/tiny_dungeon_packed.png", SDL_SCALEMODE_NEAREST);
	// state->bg    = itu_resources_texture_create(context, "data/kenney/prototype_texture_dark/texture_13.png", SDL_SCALEMODE_LINEAR);
}

static void load_tileset(EngineContext* context, E03_GameState* state)
{
	for (int i = 0; i < TILESHEET_WIDTH; ++i)
	{
		for (int j = 0; j < TILESHEET_HEIGHT; ++j) 
		{
			Sprite* sprite = &state->tilemap->tileset[j*12+i];
			itu_lib_sprite_init(
				sprite,
				state->atlas,
				itu_lib_sprite_get_source_rect(i, j, 16, 16)
			);
		}
	}
}

static void create_pattern(E03_GameState* state) 
{
	// Corners
	state->tilemap->pattern[0][0] = 16;
	state->tilemap->pattern[0][MAP_HEIGHT-1] = 4;
	state->tilemap->pattern[MAP_WIDTH-1][0] = 17;
	state->tilemap->pattern[MAP_WIDTH-1][MAP_HEIGHT-1] = 5;

	// Edges
	for (int i = 1; i < MAP_WIDTH-1; ++i)
	{
		state->tilemap->pattern[i][0] = 2;
		state->tilemap->pattern[i][MAP_HEIGHT-1] = 26;
	}
	
	for (int i = 1; i < MAP_HEIGHT-1; ++i)
	{
		state->tilemap->pattern[0][i] = 15;
		state->tilemap->pattern[MAP_WIDTH-1][i] = 13;
	}

	// Internal
	for (int i = 1; i < MAP_WIDTH-1; ++i)
	{
		for (int j = 1; j < MAP_HEIGHT-1; ++j) 
		{
			if (sin(i*j) > 0.95) 
				state->tilemap->pattern[i][j] = 12;
			else
				state->tilemap->pattern[i][j] = 0;
		}
	}
}

static void game_reset(EngineContext* context, E03_GameState* state)
{
	state->entities_alive_count = 0;

	load_tileset(context, state);
	create_pattern(state);
	state->tilemap->pos.x = -2.5f;
	state->tilemap->pos.y = -2.5f;

	{
		state->player = entity_create(state);
		state->player->transform.position = VEC2F_ZERO;
		state->player->transform.scale = VEC2F_ONE;
		itu_lib_sprite_init(
			&state->player->sprite,
			state->atlas,
			itu_lib_sprite_get_source_rect(0, 7, 16, 16)
		);

		// raise sprite a bit, so that the position concides with the center of the image
		state->player->sprite.pivot.y = 0.3f;
	}
}

static void game_update(EngineContext* context, E03_GameState* state)
{
	{
		const float player_speed = 10;

		E03_Entity* entity = state->player;
		vec2f mov = { 0 };
		if(context->btn_isdown_up)
			mov.y += 1;
		if(context->btn_isdown_down)
			mov.y -= 1;
		if(context->btn_isdown_left)
			mov.x -= 1;
		if(context->btn_isdown_right)
			mov.x += 1;
	

		SDL_GetMouseState(&context->mouse_pos.x, &context->mouse_pos.y);
		entity->transform.position = entity->transform.position + mov * (player_speed * context->delta);

		// camera follows player
		context->camera_active->world_position = entity->transform.position;
	}
}

static void render_tileset(EngineContext* context, E03_GameState* state)
{
	vec2f p_mouse_world = itu_lib_context_point_screen_to_global(context, context->mouse_pos);
	

	for (int i = 0; i < MAP_WIDTH; ++i)
	{
		for (int j = 0; j < MAP_HEIGHT; ++j)
		{
			Transform2D transform;
			transform.position.x = state->tilemap->pos.x + i;
			transform.position.y = state->tilemap->pos.y + j;
			transform.scale = VEC2F_ONE;
			transform.rotation = 0.0f;
			if (i + state->tilemap->pos.x + 0.5f == (int) p_mouse_world.x && j + state->tilemap->pos.y + 0.5f == (int) p_mouse_world.y)
				transform.position.x += 0.2f;	

			if(DEBUG_render_textures)
				itu_lib_sprite_render(
					context, 
					&state->tilemap->tileset[state->tilemap->pattern[i][j]],
					&transform
				);
			if(DEBUG_render_outlines)
				itu_lib_sprite_render_debug(
					context,
					&state->tilemap->tileset[state->tilemap->pattern[i][j]],
					&transform
				);
		}
	}
}

static void game_render(EngineContext* context, E03_GameState* state)
{
	render_tileset(context, state);
	for(int i = 0; i < state->entities_alive_count; ++i)
	{
		E03_Entity* entity = &state->entities[i];
		// render texture
		SDL_FRect rect_src = entity->sprite.rect;
		SDL_FRect rect_dst;

		if(DEBUG_render_textures)
			itu_lib_sprite_render(context, &entity->sprite, &entity->transform);

		if(DEBUG_render_outlines)
			itu_lib_sprite_render_debug(context, &entity->sprite, &entity->transform);
	}

	// debug window
	SDL_SetRenderDrawColor(context->renderer, 0xFF, 0x00, 0xFF, 0xff);
	SDL_RenderRect(context->renderer, NULL);
}

int main(void)
{
	EngineConfig config;
	config.application_name = "ES03 - Coordinate Systems";
	config.texture_pixels_per_unit = 16;
	config.camera_pixel_per_unit = 64;
	config.step_per_second_fluid = 60;

	bool quit = false;
	SDL_Window* window;
	EngineContext context = { 0 };
	E03_GameState  state   = { 0 };

	itu_lib_context_init(&config, &context);
	itu_lib_context_set_active_camera(&context, &context.camera_default);

	game_init(&context, &state);
	game_reset(&context, &state);

	itu_lib_context_frame_timing_setup(&context);

	while(!quit)
	{
		// input
		SDL_Event event;
		itu_lib_input_clear(&context);
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_EVENT_QUIT:
					quit = true;
					break;

				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_KEY_UP:
					switch(event.key.key)
					{
						case SDLK_W: itu_lib_input_key_process(&context, BTN_TYPE_UP, &event);        break;
						case SDLK_A: itu_lib_input_key_process(&context, BTN_TYPE_LEFT, &event);      break;
						case SDLK_S: itu_lib_input_key_process(&context, BTN_TYPE_DOWN, &event);      break;
						case SDLK_D: itu_lib_input_key_process(&context, BTN_TYPE_RIGHT, &event);     break;
						case SDLK_Q: itu_lib_input_key_process(&context, BTN_TYPE_ACTION_0, &event);  break;
						case SDLK_E: itu_lib_input_key_process(&context, BTN_TYPE_ACTION_1, &event);  break;
						case SDLK_SPACE: itu_lib_input_key_process(&context, BTN_TYPE_SPACE, &event); break;
					}

					// debug keys
					if(event.key.down && !event.key.repeat)
					{
						switch(event.key.key)
						{
							case SDLK_TAB: game_reset(&context, &state); break;
							case SDLK_F1: DEBUG_render_textures = !DEBUG_render_textures; break;
							case SDLK_F2: DEBUG_render_outlines = !DEBUG_render_outlines; break;
						}
					}
					break;
			}
		}

		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x00);
		SDL_RenderClear(context.renderer);

		// update
		game_update(&context, &state);
		game_render(&context, &state);
		
#ifdef ENABLE_DIAGNOSTICS
		{
			SDL_SetRenderScale(context.renderer, 1.5f, 1.5f); // Render at 1.5x scale

			SDL_SetRenderDrawColor(context.renderer, 0x0, 0x00, 0x00, 0xCC);
			SDL_FRect rect = SDL_FRect{ 5, 5, 255, 85 };
			SDL_RenderFillRect(context.renderer, &rect);

			SDL_SetRenderDrawColor(context.renderer, 0xFF, 0xFF, 0xFF, 0xFF);
			SDL_RenderDebugTextFormat(context.renderer, 10, 10, "work: %9.6f ms/f", (float)context.elapsed_work  / (float)MILLIS(1));
			SDL_RenderDebugTextFormat(context.renderer, 10, 20, "tot : %9.6f ms/f", (float)context.elapsed_frame / (float)MILLIS(1));
			SDL_RenderDebugTextFormat(context.renderer, 10, 30, "[TAB] reset ");
			SDL_RenderDebugTextFormat(context.renderer, 10, 40, "[F1]  render textures   %s", DEBUG_render_textures   ? " ON" : "OFF");
			SDL_RenderDebugTextFormat(context.renderer, 10, 50, "[F2]  render outlines   %s", DEBUG_render_outlines   ? " ON" : "OFF");
			SDL_RenderDebugTextFormat(context.renderer, 10, 60, "Mouse on screen: %4.2f, %4.2f", context.mouse_pos.x, context.mouse_pos.y);
			vec2f p_mouse_world = itu_lib_context_point_screen_to_global(&context, context.mouse_pos);
			SDL_RenderDebugTextFormat(context.renderer, 10, 70, "Mouse in world: %4.2f, %4.2f", p_mouse_world.x, p_mouse_world.y);
			vec2f p_mouse_camera = itu_lib_context_point_screen_to_window(&context, context.mouse_pos);
			SDL_RenderDebugTextFormat(context.renderer, 10, 80, "Mouse in camera: %4.2f, %4.2f", p_mouse_camera.x, p_mouse_camera.y);

			SDL_SetRenderScale(context.renderer, 1.0f, 1.0f); // Reset render back to 1x
		}
#endif
		// render
		SDL_RenderPresent(context.renderer);

		itu_lib_context_frame_timing_update(&context);
	}
}
