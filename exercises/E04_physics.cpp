#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>


const int ENTITY_COUNT = 1024;

const int COLLISION_FILTER_PLAYER         = 0b00001;
const int COLLISION_FILTER_GROUND         = 0b00010;
const int COLLISION_FILTER_CLUTTER        = 0b00100;
const int COLLISION_FILTER_CLUTTER_SENSOR = 0b01000;
const float GRAVITY      = -9.8f;
bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = false;
bool DEBUG_physics = true;
int DEBUG_simulation_type_current = 0;

const char* const DEBUG_simulation_types[] =
{
	"Dynamic", "\"Kinematic\""
};

b2DebugDraw debug_draw;

enum SimulationType
{
	SIMULATION_TYPE_DYNAMIC,
	SIMULATION_TYPE_KINEMATIC,
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
	// definitions
	float h;   // jump height
	float x_h; // jump horizontal distance

	bool grounded;

	// runtime (jupm)
	float g;   // gravity (for current jump)
	vec2f p_0; // initial position (for current jump)
	float v_0; // initial VERTICAL velocity (for current jump)
	float v_x; // initial foot speed (for current jump)
	float t_h; // jump duration (for current jump)
};

static float player_dynamic_gravity = -9.8f;
static float player_dynamic_jump_impulse = 3;
static float player_dynamic_mov_force = 10;

struct E04_GameState
{
	// shortcut references
	E04_Entity* player;

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
	SDL_assert(entity < state->entities || entity > state->entities + ENTITY_COUNT);

	--state->entities_alive_count;
	*entity = state->entities[state->entities_alive_count];
}

void compute_jump_parameters(E04_PlayerData* data)
{
	data->grounded = false;
	data->v_0 = (2*data->h*data->v_x) / (data->x_h);
	data->g   = (-2*data->h * data->v_x * data->v_x) / (data->x_h * data->x_h);
}

void clutter_apply_impulse_random(b2ShapeId clutte_entity, b2Vec2 direction, float amount, float spread)
{

	float angle = (SDL_randf() - 0.5f) * spread;

	b2Vec2 point   = b2Vec2_zero;
	b2Vec2 impulse = b2RotateVector(b2MakeRot(angle), direction);
	impulse = b2MulSV(amount, impulse);

	b2BodyId body_id = b2Shape_GetBody(clutte_entity);
	b2Body_ApplyLinearImpulse(body_id, impulse, point, true);
}

static void game_init(EngineContext* context, E04_GameState* state)
{
	itu_lib_input_set_mapping_keyboard(context, SDLK_W, BTN_TYPE_UP);
	itu_lib_input_set_mapping_keyboard(context, SDLK_A, BTN_TYPE_LEFT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_S, BTN_TYPE_DOWN);
	itu_lib_input_set_mapping_keyboard(context, SDLK_D, BTN_TYPE_RIGHT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_Q, BTN_TYPE_ACTION_0);
	itu_lib_input_set_mapping_keyboard(context, SDLK_E, BTN_TYPE_ACTION_1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_SPACE, BTN_TYPE_SPACE);

	itu_lib_input_set_mapping_keyboard(context, SDLK_F1, BTN_TYPE_DEBUG_F1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_TAB, BTN_TYPE_DEBUG_RESET);

	// allocate memory
	state->entities = (E04_Entity*)SDL_calloc(ENTITY_COUNT, sizeof(E04_Entity));
	SDL_assert(state->entities);

	state->world_id = { 0 };

	state->player_data = { 0 };
	state->player_data.g = -66.67f;
	state->player_data.h   = 3.0f;
	state->player_data.x_h = 1.5f;
	state->player_data.v_x = 5.0f;

	// texture atlases
	state->atlas = itu_resources_texture_create(context, "data/kenney/tiny_dungeon_packed.png", SDL_SCALEMODE_NEAREST);
}

static void game_reset(EngineContext* context, E04_GameState* state)
{
	// TMP reset uptime (should probably be two different variables
	context->uptime = 0;

	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);
	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = DEBUG_simulation_type_current == SIMULATION_TYPE_KINEMATIC
		? GRAVITY
		: player_dynamic_gravity;
	state->world_id = b2CreateWorld(&def_world);

	state->entities_alive_count = 0;

	// player
	{
		E04_Entity* entity = entity_create(state);
		state->player = entity;
		entity->transform.position = VEC2F_ZERO;
		entity->transform.scale = VEC2F_ONE;
		itu_lib_sprite_init(
			&entity->sprite,
			state->atlas,
			itu_lib_sprite_get_source_rect(0, 9, 16, 16)
		);
		entity->sprite.pivot.y = 0;

		// box2d body, shape and polygon
		{
			vec2f size = itu_lib_sprite_get_world_size(context, &entity->sprite, &entity->transform);
			vec2f offset = -mul_element_wise(size, entity->sprite.pivot - vec2f{ 0.5f, 0.5f });

			b2BodyDef body_def = b2DefaultBodyDef();
			body_def.type = b2_dynamicBody;
			body_def.fixedRotation = true;
			body_def.position = b2Vec2{ 0, 0 };

			b2ShapeDef shape_def = b2DefaultShapeDef();
			shape_def.density = 1; // NOTE: default density of 0 will mess with collisions and gravity!
			shape_def.enableSensorEvents  = true;
			shape_def.enableContactEvents = true;
			shape_def.enableHitEvents     = true;
			shape_def.filter.categoryBits = COLLISION_FILTER_PLAYER;
			shape_def.filter.maskBits = COLLISION_FILTER_GROUND | COLLISION_FILTER_CLUTTER_SENSOR;
			b2Polygon polygon = b2MakeOffsetBox(size.x / 2, size.y / 2, value_cast(b2Vec2, offset), b2MakeRot(entity->transform.rotation));
			b2Circle circle;
			circle.radius = 0.5f;
			circle.center = value_cast(b2Vec2, offset);
			entity->body_id = b2CreateBody(state->world_id, &body_def);
			b2CreateCircleShape(entity->body_id, &shape_def, &circle);
		}
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

#if 1
	// clutter
	{
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.fixedRotation = false;

		// collider shape (to enable collisions with the ground)
		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.density = 1;
		shape_def.filter.categoryBits = COLLISION_FILTER_CLUTTER;

		// sensor shape (to enable interaction with the player)
		b2ShapeDef shape_def_clutter = b2DefaultShapeDef();
		shape_def_clutter.density = 0;
		shape_def_clutter.isSensor = true;
		shape_def_clutter.enableSensorEvents = true;
		shape_def_clutter.filter.categoryBits = COLLISION_FILTER_CLUTTER_SENSOR;
		shape_def_clutter.filter.maskBits     = COLLISION_FILTER_PLAYER;

		b2Polygon polygon_box = b2MakeBox(0.5f, 0.5f);
		for(int i = 0; i < 32; ++i)
		{
			E04_Entity* entity = entity_create(state);
			entity->transform.scale = VEC2F_ONE;

			vec2f size = itu_lib_sprite_get_world_size(context, &entity->sprite, &entity->transform);
			vec2f offset = -mul_element_wise(size, entity->sprite.pivot - vec2f{ 0.5f, 0.5f });

			body_def.position = b2Vec2{ 3.0f + (i % 4) * 1.5f, (i / 4) * 3.0f };
			body_def.rotation = b2MakeRot(SDL_randf() * TAU);
			body_def.angularVelocity = 1;
			entity->body_id = b2CreateBody(state->world_id, &body_def);
			b2CreatePolygonShape(entity->body_id, &shape_def, &polygon_box);
			b2CreatePolygonShape(entity->body_id, &shape_def_clutter, &polygon_box);
			itu_lib_sprite_init(
				&entity->sprite,
				state->atlas,
				itu_lib_sprite_get_source_rect(3, 5, 16, 16)
			);
		}
	}
#endif

	// debug draw
	debug_draw.context = context;
	debug_draw.drawShapes = true;
	debug_draw.DrawSolidPolygonFcn = fn_box2d_wrapper_draw_polygon;
	debug_draw.DrawSolidCircleFcn  = fn_box2d_wrapper_draw_circle;
}

static void game_update(EngineContext* context, E04_GameState* state)
{
	if(context->btn_isjustpressed[BTN_TYPE_DEBUG_RESET])
		game_reset(context, state);

	// player
	{
		E04_Entity* player = state->player;
		E04_PlayerData* data = &state->player_data;

		switch(DEBUG_simulation_type_current)
		{
			case SIMULATION_TYPE_DYNAMIC:
			{
				vec2f force = VEC2F_ZERO;
				vec2f impulse = VEC2F_ZERO;
				if(data->grounded)
				{
					if(context->btn_isdown[BTN_TYPE_LEFT])
						force.x = -player_dynamic_mov_force;
					if(context->btn_isdown[BTN_TYPE_RIGHT])
						force.x = player_dynamic_mov_force;
					if(context->btn_isdown[BTN_TYPE_SPACE])
						impulse.y = player_dynamic_jump_impulse;
				}

				b2Body_ApplyForceToCenter(player->body_id, value_cast(b2Vec2, force), true);
				b2Body_ApplyLinearImpulseToCenter(player->body_id, value_cast(b2Vec2, impulse), true);
				break;
			}
			case SIMULATION_TYPE_KINEMATIC:
			{
				vec2f velocity = player->velocity;
				if(data->grounded)
				{
					if(context->btn_isdown[BTN_TYPE_LEFT])
						velocity.x = -data->v_x;
					else if(context->btn_isdown[BTN_TYPE_RIGHT])
						velocity.x = data->v_x;
					else
						velocity.x = 0;

					if(context->btn_isjustpressed[BTN_TYPE_SPACE])
					{
						compute_jump_parameters(data);
						velocity.y = state->player_data.v_0;
					}
				}
				else
				{
					if(velocity.y > 0)
						velocity.y += state->player_data.g * context->delta;
					else
						velocity.y += state->player_data.g * context->delta * 3;
				}

				b2Body_SetLinearVelocity(player->body_id, value_cast(b2Vec2, velocity));
				break;
			}
		}
	}

	// NOTE: we are compouding precision errors here (config specifies frequency in steps per second, we convert to period in nanos,
	//       and here we convert back to seconds), but specifying what you want once and expressing everything else in function of
	//       it avoids mismatching. A more advanced implementation would have independent loop frequencies for game and physics and
	//       synch them under the hood (you can try it in exercise 04.2, will be discussed in class during exercise review)
	b2World_Step(state->world_id, NS_TO_SECONDS(context->target_framerate_fixed_ns), 4);

	// entities
	for(int i = 0; i < state->entities_alive_count; ++i)
	{
		E04_Entity* entity = &state->entities[i];
		b2Vec2 physics_vel = b2Body_GetLinearVelocity(entity->body_id);
		b2Vec2 physics_pos = b2Body_GetPosition(entity->body_id);
		b2Rot  physics_rot = b2Body_GetRotation(entity->body_id);
		entity->velocity = value_cast(vec2f, physics_vel);
		entity->transform.position = value_cast(vec2f, physics_pos);
		entity->transform.rotation = b2Rot_GetAngle(physics_rot);
	}

	// player
	{
		E04_Entity* entity = state->player;
		E04_PlayerData* data = &state->player_data;


		// NOTE: quick hack because I forgot a VLA in the code again. Exercise solution will do this nicely
		static int contad_data_size = 16;
		static b2ContactData* contact_data = (b2ContactData*)SDL_calloc(16, sizeof(b2ContactData));

		int contacts = b2Body_GetContactCapacity(entity->body_id);

		if(contacts > contad_data_size)
		{
			contad_data_size = contacts;
			contact_data = (b2ContactData*)SDL_realloc(contact_data, contad_data_size * sizeof(b2ContactData));
		}
		int actual_contacts = b2Body_GetContactData(state->player->body_id, contact_data, contacts);

		data->grounded = false;
		for(int i = 0; i < actual_contacts; ++i)
		{
			b2Filter filter_a = b2Shape_GetFilter(contact_data[i].shapeIdA);
			b2Filter filter_b = b2Shape_GetFilter(contact_data[i].shapeIdB);
			if(filter_a.categoryBits & COLLISION_FILTER_GROUND)
				data->grounded = true;
		}
	}

	// world
	b2SensorEvents worl_sensor_events = b2World_GetSensorEvents(state->world_id);
	for(int i = 0; i < worl_sensor_events.beginCount; ++i)
	{
		b2SensorBeginTouchEvent* sensor_event = &worl_sensor_events.beginEvents[i];
		b2Vec2 direction = b2Vec2 { 0, 1 };

		float vel_sq = length_sq(state->player->velocity);
		if(SDL_fabsf(vel_sq) < FLOAT_EPSILON)
			// apply impulse only if the player is moving
			// (boxes falling on player when it's not moving feel unnatural)
			continue;

		float amount = SDL_clamp(length_sq(state->player->velocity) * 2, 5, 15);
		float spread = state->player_data.grounded ? PI / 4 : TAU;
		clutter_apply_impulse_random(sensor_event->sensorShapeId, direction, amount, spread);
	}

	{
		const float zoom_speed = 1;
		vec2f camera_offset = vec2f { 0.0f, 3.0f } / context->camera_active->zoom;
		// camera follows player
		context->camera_active->world_position = state->player->transform.position + camera_offset;
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
}

int main(void)
{
	bool quit = false;
	EngineConfig config = { 0 };
	EngineContext context = { 0 };
	E04_GameState  state   = { 0 };

	config.application_name = "E04 - Physics";
	config.window_w = 800;
	config.window_h = 600;
	config.step_per_second_fixed = 60;
	config.step_per_second_fluid = 60;
	config.texture_pixels_per_unit = 16;
	config.camera_pixel_per_unit = 32;

	itu_lib_context_init(&config, &context);
	itu_lib_imgui_setup(&context, true);

	itu_lib_context_set_active_camera(&context, &context.camera_default);

	game_init(&context, &state);
	game_reset(&context, &state);


	itu_lib_context_frame_timing_setup(&context);

	while(!quit)
	{
		// input
		quit = itu_lib_input_process_events(&context);

		if(context.btn_isjustpressed[BTN_TYPE_DEBUG_F1])
			context.debug_ui_show = !context.debug_ui_show;

		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0x00);
		SDL_RenderClear(context.renderer);

		itu_lib_imgui_frame_begin(&context);

		// update
		game_update(&context, &state);
		game_render(&context, &state);

#ifdef ENABLE_DIAGNOSTICS
		if(context.debug_ui_show)
		{
			ImGui::Begin("itu_diagnostics", &context.debug_ui_show, 0);
			ImGui::PushItemWidth(200);
			ImGui::SeparatorText("Timing");
			ImGui::LabelText("work", "%6.3f ms/f", (float)context.elapsed_work / (float)MILLIS(1));
			ImGui::LabelText("tot", "%6.3f ms/f", (float)context.elapsed_frame / (float)MILLIS(1));


			ImGui::SeparatorText("Simulation config");
			if(ImGui::Combo("Simulation type", &DEBUG_simulation_type_current, DEBUG_simulation_types, array_size(DEBUG_simulation_types)))
				// reset simulation on type change
				game_reset(&context, &state);
			b2Vec2 player_pos_physics = b2Body_GetPosition(state.player->body_id);
			ImGui::LabelText("player pos game ", "%4.2f   %4.2f", state.player->transform.position.x, state.player->transform.position.y);
			ImGui::LabelText("player pos box2d", "%4.2f   %4.2f", player_pos_physics.x, player_pos_physics.y);
			b2Vec2 velocity = b2Body_GetLinearVelocity(state.player->body_id);
			ImGui::LabelText("player velocity", "%4.2f   %4.2f", velocity.x, velocity.y);

			ImGui::SeparatorText("Simulation type params");
			switch(DEBUG_simulation_type_current)
			{
				case SIMULATION_TYPE_DYNAMIC:
				{
					if(ImGui::DragFloat("gravity", &player_dynamic_gravity))
						b2World_SetGravity(state.world_id, b2Vec2 { 0, player_dynamic_gravity });
					ImGui::DragFloat("jump impulse", &player_dynamic_jump_impulse);
					ImGui::DragFloat("mov force", &player_dynamic_mov_force);
					break;
				}
				case SIMULATION_TYPE_KINEMATIC:
				{
					ImGui::DragFloat("horizontal velocity", &state.player_data.v_x);
					ImGui::DragFloat("jump height", &state.player_data.h);
					ImGui::DragFloat("jump distance", &state.player_data.x_h);
					ImGui::LabelText("jump gravity", "%4.2f", state.player_data.g);
					ImGui::LabelText("jump initial vertical velocity", "%4.2f", state.player_data.v_0);
					break;
				}
			}

			ImGui::SeparatorText("Debug");
			if(ImGui::Button("[TAB] reset"))
				game_reset(&context, &state);
			ImGui::Checkbox("render textures", &DEBUG_render_textures);
			ImGui::Checkbox("render outlines", &DEBUG_render_outlines);
			ImGui::Checkbox("render physics", &DEBUG_physics);

			ImGui::PopItemWidth();
			ImGui::End();
		}
#endif

		itu_lib_imgui_frame_end(&context);

		// render
		SDL_RenderPresent(context.renderer);

		itu_lib_context_frame_timing_update(&context);
	}
}
