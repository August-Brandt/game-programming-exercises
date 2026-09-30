#include "itu_lib_context.hpp"
#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>

const int ENTITY_COUNT = 1024;

const int COLLISION_FILTER_PLAYER         = 0b00001;
const int COLLISION_FILTER_GROUND         = 0b00010;
const int COLLISION_FILTER_CLUTTER        = 0b00100;
const int COLLISION_FILTER_PROJECTILE = 0b01000;
const float GRAVITY      = -9.8f;


bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = false;
bool DEBUG_physics = true;

b2DebugDraw debug_draw;

enum GameplayPhase
{
	GAMEPLAY_PHASE_AIM,
	GAMEPLAY_PHASE_WAIT
};

struct E04_Entity
{
	Sprite      sprite;
	Transform2D transform;
	b2BodyId    body_id;
	vec2f       velocity;
};

struct E04_PlayerData
{
	float shooting_angle;
	float shooting_power;
	float keyboard_angle_speed;
	float keyboard_power_speed;
};

struct E04_GameState
{
	// shortcut references
	E04_Entity* player;
	E04_Entity* projectile;

	// game-allocated memory
	E04_Entity* entities;
	int entities_alive_count;
	E04_PlayerData player_data;

	// SDL-allocated structures
	SDL_Texture* atlas;

	// box2d
	b2WorldId world_id;
};

static E04_Entity* entity_create(E04_GameState* state)
{
	if(!(state->entities_alive_count < ENTITY_COUNT))
		// NOTE: this might as well be an assert, if we don't have a way to recover/handle it
		return NULL;

	// // concise version
	//return &state->entities[state->entities_alive_count++];

	E04_Entity* ret = &state->entities[state->entities_alive_count];
	++state->entities_alive_count;
	return ret;
}

// NOTE: this only works if nobody holds references to other entities!
//       if that were the case, we couldn't swap them around.
//       We will see in later lectures how to handle this kind of problems
static void entity_destroy(E04_GameState* state, E04_Entity* entity)
{
	// NOTE: here we want to fail hard, nobody should pass us a pointer not gotten from `entity_create()`
	SDL_assert(entity < state->entities ||entity > state->entities + ENTITY_COUNT);

	--state->entities_alive_count;
	*entity = state->entities[state->entities_alive_count];
}


// game parameters
float design_cannon_length = 4;
float design_shooting_angle_min = PI_HALF / 8;
float design_shooting_angle_max = PI_HALF - design_shooting_angle_min;
float design_shooting_power_min = 1;
float design_shooting_power_max = 50;


void debug_player_data(EngineContext* context, E04_GameState* state)
{
	E04_PlayerData* data = &state->player_data;
	
	// show angle as degrees to the user (easier to understand)
	float shooting_angle_deg = data->shooting_angle * RAD_2_DEG;
	float shooting_angle_deg_min = design_shooting_power_min * RAD_2_DEG;
	float shooting_angle_deg_max = design_shooting_power_max * RAD_2_DEG;

	ImGui::Begin("game_player_data");
	if(ImGui::DragFloat("shooting_angle", &shooting_angle_deg, 1.0f, shooting_angle_deg_min, shooting_angle_deg_max))
		data->shooting_angle = shooting_angle_deg * DEG_2_RAD;
	ImGui::DragFloat("shooting_power", &data->shooting_power, 1.0f, design_shooting_power_min, design_shooting_power_max);
	ImGui::DragFloat("keyboard_angle_speed", &data->keyboard_angle_speed, 1.0f);
	ImGui::DragFloat("keyboard_power_speed", &data->keyboard_power_speed, 1.0f);
	ImGui::End();

}

static void get_physics_data(E04_GameState* state)
{
	for(int i = 0; i < state->entities_alive_count; ++i)
	{
		E04_Entity* entity = &state->entities[i];
		if(!b2Body_IsValid(entity->body_id))
			continue;
		if(b2Body_GetType(entity->body_id) == b2_staticBody)
			continue;

		b2Vec2 physics_vel = b2Body_GetLinearVelocity(entity->body_id);
		b2Vec2 physics_pos = b2Body_GetPosition(entity->body_id);
		b2Rot  physics_rot = b2Body_GetRotation(entity->body_id);
		entity->velocity = value_cast(vec2f, physics_vel); 
		entity->transform.position = value_cast(vec2f, physics_pos);
		entity->transform.rotation = b2Rot_GetAngle(physics_rot);
	}
}

static void game_init(EngineContext* context, E04_GameState* state)
{
	// register inputs
	itu_lib_input_set_mapping_keyboard(context, SDLK_W, BTN_TYPE_UP);
	itu_lib_input_set_mapping_keyboard(context, SDLK_S, BTN_TYPE_DOWN);
	itu_lib_input_set_mapping_keyboard(context, SDLK_A, BTN_TYPE_LEFT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_D, BTN_TYPE_RIGHT);
	itu_lib_input_set_mapping_mouse(context, 1, BTN_TYPE_ACTION_0);


	itu_lib_input_set_mapping_keyboard(context, SDLK_F1, BTN_TYPE_DEBUG_F1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_F2, BTN_TYPE_DEBUG_F2);
	itu_lib_input_set_mapping_keyboard(context, SDLK_F3, BTN_TYPE_DEBUG_F3);
	itu_lib_input_set_mapping_keyboard(context, SDLK_TAB, BTN_TYPE_DEBUG_RESET);

	// allocate memory
	state->entities = (E04_Entity*)SDL_calloc(ENTITY_COUNT, sizeof(E04_Entity));
	SDL_assert(state->entities);

	state->world_id = { 0 };
	
	state->player_data = { 0 };
	state->player_data.shooting_angle = (design_shooting_angle_max - design_shooting_angle_min) / 2;
	state->player_data.shooting_power = design_shooting_power_min;
	state->player_data.keyboard_angle_speed = 1;
	state->player_data.keyboard_power_speed = 10;

	// texture atlases
	state->atlas = itu_resources_texture_create(context, "data/kenney/tiny_dungeon_packed.png", SDL_SCALEMODE_NEAREST);
}

static void game_reset(EngineContext* context, E04_GameState* state)
{
	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);
	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = GRAVITY;
	state->world_id = b2CreateWorld(&def_world);

	state->entities_alive_count = 0;

	// player cannon
	{
		E04_Entity* entity = entity_create(state);
		state->player = entity;
		entity->transform.position = vec2f { -15, -2 };
		entity->transform.scale = vec2f { design_cannon_length, 1 };
		itu_lib_sprite_init(&entity->sprite, state->atlas, itu_lib_sprite_get_source_rect(0, 0, 16, 16));
		entity->sprite.pivot.x = 1 / (design_cannon_length * 2);
	}

	// projectile
	{
		E04_Entity* entity = entity_create(state);
		state->projectile = entity;
		entity->transform.scale = vec2f { 1, 1 };
		itu_lib_sprite_init(&entity->sprite, state->atlas, itu_lib_sprite_get_source_rect(10, 6, 16, 16));
		
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.position = b2Vec2{ 99, 99 };
		body_def.isEnabled = false;

		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.filter.categoryBits = COLLISION_FILTER_PROJECTILE;

		b2Polygon polygon = b2MakeBox(0.3f, 0.5f);

		entity->body_id = b2CreateBody(state->world_id, &body_def);
		b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
	}

	// floor
	{
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;
		body_def.position = b2Vec2{ 0, -3 };
		b2ShapeDef shape_def = b2DefaultShapeDef();

		shape_def.filter.categoryBits = COLLISION_FILTER_GROUND;
		b2Polygon polygon = b2MakeBox(32.0f, 1.0f);

		E04_Entity* entity = entity_create(state);
		entity->body_id = b2CreateBody(state->world_id, &body_def);
		b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
	}

	// clutter
	{
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.fixedRotation = false;

		// collider shape (to enable collisions with the ground)
		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.density = 1;
		shape_def.filter.categoryBits = COLLISION_FILTER_CLUTTER;

		b2Polygon polygon_box = b2MakeBox(0.5f, 0.5f);
		for(int i = 0; i < 32; ++i)
		{
			E04_Entity* entity = entity_create(state);
			entity->transform.scale = VEC2F_ONE;
			
			vec2f size = itu_lib_sprite_get_world_size(context, &entity->sprite, &entity->transform);
			vec2f offset = -mul_element_wise(size, entity->sprite.pivot - vec2f{ 0.5f, 0.5f });

			body_def.position = b2Vec2{ 3.0f + (float)(i % 4), (float)(i / 4) - 1.5f};
			body_def.angularVelocity = 1;
			entity->body_id = b2CreateBody(state->world_id, &body_def);
			b2CreatePolygonShape(entity->body_id, &shape_def, &polygon_box);
			itu_lib_sprite_init(
				&entity->sprite,
				state->atlas,
				itu_lib_sprite_get_source_rect(3, 5, 16, 16)
			);
		}
	}

	// debug draw
	debug_draw.context = context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn = fn_box2d_wrapper_draw_circle;
}

static void game_update(EngineContext* context, E04_GameState* state)
{
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_RESET])
		game_reset(context, state);
	
	// player cannon
	{
		E04_Entity* entity = state->player;
		E04_PlayerData* data = &state->player_data;
		if(context->btn_isdown[BTN_TYPE_UP])
			data->shooting_angle += data->keyboard_angle_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_DOWN])
			data->shooting_angle -= data->keyboard_angle_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_LEFT])
			data->shooting_power -= data->keyboard_power_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_RIGHT])
			data->shooting_power += data->keyboard_power_speed * context->delta;

		data->shooting_angle = SDL_clamp(data->shooting_angle, design_shooting_angle_min, design_shooting_angle_max);
		data->shooting_power = SDL_clamp(data->shooting_power, design_shooting_power_min, design_shooting_power_max);

		entity->transform.rotation = data->shooting_angle;
	}

	// projectile
	{
		E04_Entity* entity = state->projectile;
		if(context->btn_isjustpressed[BTN_TYPE_ACTION_0])
		{
			float spawn_rotation = state->player_data.shooting_angle;
			vec2f spawn_direction = vec2f { SDL_cosf(spawn_rotation), SDL_sinf(spawn_rotation) };
			vec2f spawn_position = state->player->transform.position + spawn_direction * design_cannon_length;
			vec2f spawn_impulse = spawn_direction * state->player_data.shooting_power;
			b2Body_SetLinearVelocity(entity->body_id, b2Vec2_zero);
			b2Body_SetAngularVelocity(entity->body_id, 0);
			b2Body_SetTransform(entity->body_id, value_cast(b2Vec2, spawn_position), b2MakeRot(spawn_rotation));
			b2Body_ApplyLinearImpulseToCenter(entity->body_id, value_cast(b2Vec2, spawn_impulse), true);
			b2Body_Enable(entity->body_id);
		}
	}
}

static void game_update_post_physics(EngineContext* context, E04_GameState* state)
{
	// camera
	{
		const float zoom_speed = 1;
		vec2f camera_offset = vec2f { -5.0f, 4.0f } / context->camera_active->zoom;
		context->camera_active->world_position = camera_offset;
		context->camera_active->zoom += context->mouse_scroll * zoom_speed * context->delta;
	}
}

static void game_render(EngineContext* context, E04_GameState* state)
{
	itu_lib_render_draw_world_grid(context);
	
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F1]) DEBUG_render_textures = !DEBUG_render_textures;
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F2]) DEBUG_render_outlines = !DEBUG_render_outlines;
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_F3]) DEBUG_physics = !DEBUG_physics;

	// entities
	for(int i = 0; i < state->entities_alive_count; ++i)
	{
		E04_Entity* entity = &state->entities[i];
		// render texture
		SDL_FRect rect_src = entity->sprite.rect;
		SDL_FRect rect_dst;

		if(DEBUG_render_textures)
			itu_lib_sprite_render(context, &entity->sprite, &entity->transform);

		if(DEBUG_render_outlines)
			itu_lib_sprite_render_debug(context, &entity->sprite, &entity->transform);
	}

	if(DEBUG_physics)
		b2World_Draw(state->world_id, &debug_draw);

	// debug window
	itu_lib_render_draw_world_point(context, VEC2F_ZERO, 10, color { 1, 0, 1, 1 });

	SDL_SetRenderDrawColor(context->renderer, 0xFF, 0x00, 0xFF, 0xff);
	SDL_RenderRect(context->renderer, NULL);

	// imgui debug windows
	{
		debug_player_data(context, state);
	}
}

int main(void)
{
	EngineConfig config = { 0 };
	bool quit = false;
	SDL_Window* window;
	EngineContext context = { 0 };
	E04_GameState  state   = { 0 };

	config.application_name = "ES04.1.1 - Shooting Game";
	config.step_per_second_fixed = 60;

	itu_lib_context_init(&config, &context);
	itu_lib_imgui_setup(&context, true);

	itu_lib_context_set_active_camera(&context, &context.camera_default);


	game_init(&context, &state);
	game_reset(&context, &state);

	itu_lib_context_frame_timing_setup(&context);

	while(!quit)
	{
		quit = itu_lib_input_process_events(&context);

		if(context.btn_isjustpressed[BTN_TYPE_DEBUG_F1])
			context.debug_ui_show = !context.debug_ui_show;
		
		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x00);
		SDL_RenderClear(context.renderer);

		itu_lib_imgui_frame_begin(&context);

		// update
		game_update(&context, &state);

		// physics
		{
			context.physics_steps_per_frame_count = 0;
			while(context.accumulator_physics >= context.target_framerate_fixed_ns && context.physics_steps_per_frame_count < config.physics_steps_per_frame_max)
			{
				b2World_Step(state.world_id, NS_TO_SECONDS(context.target_framerate_fixed_ns), 4);
				++context.physics_steps_per_frame_count;
				context.accumulator_physics -= context.target_framerate_fixed_ns;

				get_physics_data(&state);

				game_update_post_physics(&context, &state);
			}
		}

		game_render(&context, &state);

#ifdef ENABLE_DIAGNOSTICS
		if(context.debug_ui_show)
		{
			ImGui::Begin("itu_diagnostics");
			ImGui::PushItemWidth(200);
			ImGui::Text("Timing");
			ImGui::LabelText("work", "%6.3f ms/f", (float)context.elapsed_work / (float)MILLIS(1));
			ImGui::LabelText("frame", "%6.3f ms/f", (float)context.elapsed_frame / (float)MILLIS(1));
			ImGui::LabelText("physics - accumulator", "%6.3f ms/f", (float)context.accumulator_physics  / (float)MILLIS(1));
			ImGui::LabelText("physics - steps per frame", "%6d", context.physics_steps_per_frame_count);

			ImGui::Text("Debug");
			if(ImGui::Button("[TAB] reset"))
				game_reset(&context, &state);
			ImGui::Checkbox("render textures", &DEBUG_render_textures);
			ImGui::Checkbox("render outlines", &DEBUG_render_outlines);
			ImGui::Checkbox("render physics", &DEBUG_physics);
			ImGui::End();
		}
#endif
		
		itu_lib_imgui_frame_end(&context);

		// render
		SDL_RenderPresent(context.renderer);

		itu_lib_context_frame_timing_update(&context);
	}
}
