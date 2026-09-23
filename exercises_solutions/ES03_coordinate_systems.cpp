#define STB_IMAGE_IMPLEMENTATION
#define ITU_UNITY_BUILD

#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>

bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = true;
const int ENTITY_COUNT = 4096;
const int TILESET_NUM_ROWS = 11; // this could probably belong to a dedicated `Tileset` struct that contains texture pointer and metadata
const int TILESET_NUM_COLS = 12; // this could probably belong to a dedicated `Tileset` struct that contains texture pointer and metadata

// NOTE: we are storing the tilemap with the y-axis pointing up to make the math easier in code,
//       BUT this meas that the array of arrays here looks upside-down!
//       Still worth IMHO, since we wouldn't change it manually anyway but use a Graphical User Interface
// NOTE: this indirect id representation is how you would do it in a real tool (separate the logical meaning of the tile in the tilemap
//       from which tile performs that function). We didn't NEED to do this here, but I (chris) keep tweaking and changing which tile does what all
//       the time, and it is easier with an ID map that changing them all, all the time
const int tile_ids[14][11]
{
	{ 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 },
	{ 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3 },
};
const int tile_mapping[] = {
	0,	// tile coords (0, 0) in the atlas, ground tile
	1,	// tile coords (1, 0) in the atlas, wall top-left corner
	2,	// tile coords (2, 0) in the atlas, wall top side
	3,	// tile coords (3, 0) in the atlas, wall top-right corner
	13,	// tile coords (1, 1) in the atlas, wall left side
	15,	// tile coords (3, 1) in the atlas, wall right side
	25,	// tile coords (1, 2) in the atlas, wall bottom-left corner
	26,	// tile coords (2, 2) in the atlas, wall bottom side
	27  // tile coords (3, 2) in the atlas, wall bottom-right corner
};


struct E03_Entity
{
	Sprite sprite;
	Transform2D transform;
};

// NOTE at the moment, we are treanting the tilemap as an entirely independent thing,
//      even if it could share some funcitonlity with other entities. We will refine this later
struct E03_EntityTilemap
{
	Transform2D transform;

	SDL_Texture* texture;
	int num_rows;
	int num_cols;
	int tile_size;

	int* tile_ids;
};

struct E03_GameState
{
	// shortcut references
	E03_Entity* player;

	// game-allocated memory
	E03_Entity* entities;
	int entities_alive_count;

	E03_EntityTilemap tilemap;

	// SDL-allocated structures
	SDL_Texture* atlas;
	SDL_Texture* bg;
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
	SDL_assert(entity < state->entities || entity > state->entities + ENTITY_COUNT);

	--state->entities_alive_count;
	*entity = state->entities[state->entities_alive_count];
}

vec2f tilemap_point_world_to_tilemap(EngineContext* context, E03_EntityTilemap* tilemap, vec2f p)
{
	vec2f ret;
	ret.x = p.x / (tilemap->transform.scale.x * tilemap->tile_size / context->config.texture_pixels_per_unit) - tilemap->transform.position.x + 0.5f;
	ret.y = p.y / (tilemap->transform.scale.y * tilemap->tile_size / context->config.texture_pixels_per_unit) - tilemap->transform.position.y + 0.5f;
	return ret;
}

static void game_init(EngineContext* context, E03_GameState* state)
{
	// allocate memory
	state->entities = (E03_Entity*)SDL_calloc(ENTITY_COUNT, sizeof(E03_Entity));
	SDL_assert(state->entities);

	// TODO allocate space for tile info (when we'll load those from file)

	// texture atlases
	state->atlas = itu_resources_texture_create(context, "data/kenney/tiny_dungeon_packed.png", SDL_SCALEMODE_NEAREST);
	state->bg    = itu_resources_texture_create(context, "data/kenney/prototype_texture_dark/texture_13.png", SDL_SCALEMODE_LINEAR);
}

static void game_reset(EngineContext* context, E03_GameState* state)
{
	state->entities_alive_count = 0;

	// player
	{
		state->player = entity_create(state);
		state->player->transform.position = VEC2F_ZERO;
		state->player->transform.scale = VEC2F_ONE;
		itu_lib_sprite_init(
			&state->player->sprite,
			state->atlas,
			itu_lib_sprite_get_source_rect(0, 9, 16, 16)
		);
	}

	// tilemap
	{
		state->tilemap.transform.position = VEC2F_ZERO;
		state->tilemap.transform.scale = VEC2F_ONE;
		state->tilemap.texture = state->atlas;
		state->tilemap.num_rows = 14;
		state->tilemap.num_cols = 11;
		state->tilemap.tile_size = 16;
		state->tilemap.tile_ids = (int*)tile_ids;
	}
}

static void game_update(EngineContext* context, E03_GameState* state)
{
	// player
	{
		const float player_speed = 3;

		E03_Entity* entity = state->player;
		vec2f mov = { 0 };
		if(context->btn_isdown_up)    mov.y += 1;
		if(context->btn_isdown_down)  mov.y -= 1;
		if(context->btn_isdown_left)  mov.x -= 1;
		if(context->btn_isdown_right) mov.x += 1;
	
		entity->transform.position = entity->transform.position + mov * (player_speed * context->delta);
	}

	const float zoom_speed = 1;

	// camera follows player
	context->camera_active->world_position = state->player->transform.position;
	context->camera_active->zoom += context->mouse_scroll * zoom_speed * context->delta;
}

static void game_render(EngineContext* context, E03_GameState* state)
{
	// render tilemap
	{
		// offset to apply to the tile to center it (so that entities appear in the proper place)
		// (the offset is specified in tilemap-space, so we don;t have to worry about conversions)
		const float tile_offset = -0.5f;

		E03_EntityTilemap* tilemap = &state->tilemap;
		
		// NOTE: here we are pulluting the tilemap rendering loop (which we could easily extract and make a generic API) with some
		//       "gameplay" (or debug at best) logic. A better way to do this would be to store additional information per-tile,
		//       like the tint
		vec2f mouse_pos_world  = itu_lib_context_point_screen_to_global(context, context->mouse_pos);
		vec2f mouse_pos_tilemap = tilemap_point_world_to_tilemap(context, tilemap, mouse_pos_world);
		int mouse_coord_x = (int)mouse_pos_tilemap.x;
		int mouse_coord_y = (int)mouse_pos_tilemap.y;

		vec2f player_pos_tilemap = tilemap_point_world_to_tilemap(context, tilemap, state->player->transform.position);
		int player_coord_x = (int)player_pos_tilemap.x;
		int player_coord_y = (int)player_pos_tilemap.y;
		
		vec2f one_minus_pivot;
		one_minus_pivot.x = 1 - state->player->sprite.pivot.x;
		one_minus_pivot.y = 1 - state->player->sprite.pivot.y;
		vec2f player_size_world = itu_lib_sprite_get_world_size(context, &state->player->sprite, &state->player->transform);
		vec2f player_min_tilemap = tilemap_point_world_to_tilemap(context, tilemap, state->player->transform.position - mul_element_wise(player_size_world, state->player->sprite.pivot));
		vec2f player_max_tilemap = tilemap_point_world_to_tilemap(context, tilemap, state->player->transform.position + mul_element_wise(player_size_world, one_minus_pivot));

		// we could hve each tile being an independent entity, be let's do something more clever
		for(int y = 0; y < tilemap->num_rows; ++y)
		{
			for(int x = 0; x < tilemap->num_cols; ++x)
			{
				// get tile coords from tile id in the map and tile-id mapping
				int tile_id_map = tilemap->tile_ids[y * tilemap->num_cols + x];
				int tile_id_texture = tile_mapping[tile_id_map];
				int tile_coord_x = tile_id_texture % TILESET_NUM_COLS;
				int tile_coord_y = tile_id_texture / TILESET_NUM_COLS;

				// get source rect from texture size and tile coords
				SDL_FRect rect_src;
				rect_src.w = tilemap->tile_size;
				rect_src.h = tilemap->tile_size;
				rect_src.x = tile_coord_x * rect_src.w;
				rect_src.y = tile_coord_y * rect_src.h;

				// get destination rect based on current x and y
				SDL_FRect rect_dst;
				rect_dst.w = tilemap->transform.scale.x * (tilemap->tile_size / (float)context->config.texture_pixels_per_unit);
				rect_dst.h = tilemap->transform.scale.y * (tilemap->tile_size / (float)context->config.texture_pixels_per_unit);
				rect_dst.x = tilemap->transform.position.x + rect_dst.w * (x + tile_offset); // NOTE: offsetting the destination rect to center the tile
				rect_dst.y = tilemap->transform.position.y + rect_dst.h * (y + tile_offset); // NOTE: offsetting the destination rect to center the tile
				rect_dst = itu_lib_context_rect_global_to_screen(context, rect_dst);

				// set tile tint
				color tile_tint = COLOR_WHITE;
				// check mouse pos
				if(mouse_coord_x == x && mouse_coord_y == y)
					tile_tint = COLOR_RED;

				// check player center pos
				else if(player_coord_x == x && player_coord_y == y)
					tile_tint = COLOR_GREEN;

				// check player extents
				else if(
					x > player_min_tilemap.x - 0.5f + tile_offset && x < player_max_tilemap.x + 0.5f + tile_offset &&
					y > player_min_tilemap.y - 0.5f + tile_offset && y < player_max_tilemap.y + 0.5f + tile_offset
				)
					tile_tint = COLOR_BLUE;

				SDL_SetTextureColorModFloat(tilemap->texture, tile_tint.r, tile_tint.g, tile_tint.b);

				SDL_FRect foo;
				foo.x = 32;
				foo.y = 32;
				foo.w = 32;
				foo.h = 32;
				SDL_RenderTexture(context->renderer, tilemap->texture, &rect_src, &rect_dst);
				// SDL_RenderTexture(context->renderer, tilemap->texture, &rect_src, &rect_dst);
			}
		}
	}

	// entities
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

	// debug mouse
	if(DEBUG_render_outlines)
	{
		const vec2f debug_rect_size = vec2f { 200, 54 };
		vec2f mouse_pos_screen = context->mouse_pos;
		vec2f mouse_pos_world  = itu_lib_context_point_screen_to_global(context, mouse_pos_screen);
		vec2f mouse_pos_camera = mouse_pos_world - context->camera_active->world_position;
		vec2f mouse_pos_tilemap = tilemap_point_world_to_tilemap(context, &state->tilemap, mouse_pos_world);
		vec2f debug_text_pos = mouse_pos_screen;
		debug_text_pos.x -= debug_rect_size.x;

		itu_lib_render_screen_rect_fill(context->renderer, debug_text_pos, debug_rect_size, color { 0.0f, 0.0f, 0.0f, 0.8f});

		SDL_SetRenderDrawColor(context->renderer, 0xFF, 0xFF, 0xFF, 0xFF);
		SDL_RenderDebugText(context->renderer, debug_text_pos.x + 2, debug_text_pos.y + 02, "mouse pos");
		SDL_RenderDebugTextFormat(context->renderer, debug_text_pos.x + 2, debug_text_pos.y + 12, "screen  : %6.2f, %6.2f", mouse_pos_screen.x, mouse_pos_screen.y);
		SDL_RenderDebugTextFormat(context->renderer, debug_text_pos.x + 2, debug_text_pos.y + 22, "world   : %6.2f, %6.2f", mouse_pos_world.x , mouse_pos_world.y);
		SDL_RenderDebugTextFormat(context->renderer, debug_text_pos.x + 2, debug_text_pos.y + 32, "camera  : %6.2f, %6.2f", mouse_pos_camera.x, mouse_pos_camera.y);
		SDL_RenderDebugTextFormat(context->renderer, debug_text_pos.x + 2, debug_text_pos.y + 42, "tilemap : %6.2f, %6.2f", mouse_pos_tilemap.x, mouse_pos_tilemap.y);

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
	config.camera_pixel_per_unit = config.texture_pixels_per_unit * 4;
	config.step_per_second_fluid = 60;

	bool quit = false;
	EngineContext context = { 0 };
	E03_GameState  state   = { 0 };

	itu_lib_context_init(&config, &context);
	itu_lib_context_set_active_camera(&context, &context.camera_default);

	game_init(&context, &state);
	game_reset(&context, &state);

	itu_lib_context_frame_timing_update(&context);

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
				// listen for mouse motion and store the absolute position in screen space
				case SDL_EVENT_MOUSE_MOTION:
				{
					context.mouse_pos.x = event.motion.x / context.zoom;
					context.mouse_pos.y = event.motion.y / context.zoom;
					break;
				}
				// listen for mouse wheel and store the relative position in screen space
				case SDL_EVENT_MOUSE_WHEEL:
				{
					context.mouse_scroll = event.wheel.y;
					break;
				}
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
			SDL_SetRenderDrawColor(context.renderer, 0x0, 0x00, 0x00, 0xCC);
			SDL_FRect rect = SDL_FRect{ 5, 5, 225, 55 };
			SDL_RenderFillRect(context.renderer, &rect);

			SDL_SetRenderDrawColor(context.renderer, 0xFF, 0xFF, 0xFF, 0xFF);
			SDL_RenderDebugTextFormat(context.renderer, 10, 10, "work: %9.6f ms/f", (float)context.elapsed_work  / (float)MILLIS(1));
			SDL_RenderDebugTextFormat(context.renderer, 10, 20, "tot : %9.6f ms/f", (float)context.elapsed_frame / (float)MILLIS(1));
			SDL_RenderDebugTextFormat(context.renderer, 10, 30, "[TAB] reset ");
			SDL_RenderDebugTextFormat(context.renderer, 10, 40, "[F1]  render textures   %s", DEBUG_render_textures   ? " ON" : "OFF");
			SDL_RenderDebugTextFormat(context.renderer, 10, 50, "[F2]  render outlines   %s", DEBUG_render_outlines   ? " ON" : "OFF");
		}
#endif
		// render
		SDL_RenderPresent(context.renderer);

		itu_lib_context_frame_timing_update(&context);
	}
}
