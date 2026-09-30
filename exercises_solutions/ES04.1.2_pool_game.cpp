#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>

const int   ENTITY_COUNT = 1024;
const int   BALL_COUNT   = 64;
const float GRAVITY      = 0.0f;

const int COLLISION_FILTER_WALL   = 0b00001;
const int COLLISION_FILTER_HOLE   = 0b00010;
const int COLLISION_FILTER_BALL   = 0b00100;
const int COLLISION_FILTER_PLAYER = 0b01000;

bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = false;
bool DEBUG_physics = true;
b2DebugDraw debug_draw;

struct E04_Entity
{
	bool        alive;
	int         index;
	Sprite      sprite;
	Transform2D transform;
	b2BodyId    body_id;
	vec2f       velocity;
};

struct E04_PlayerData
{
	E04_Entity* entity;
	vec2f p0;
	vec2f p1;
	bool p0_placed;
};

struct E04_BallData
{
	E04_Entity* entity;
};

struct E04_GameState
{
	// shortcut references
	vec2f camera_pos;

	// game-allocated memory
	E04_Entity* entities;
	int entities_alive_count;
	E04_PlayerData player_data;
	E04_BallData   balls_data[BALL_COUNT];

	// SDL-allocated structures
	SDL_Texture* atlas;
	SDL_Texture* bg;

	// box2d
	b2WorldId world_id;
	// hashmap to retrieve entity from bodyId (which is the only thing we have when handling collision ovents from b2d)
	// NOTE: I (chris) used an hashmap to test viability in future exercises. An array + linear search would have been more than enough for our current needs)
	struct { b2BodyId key; E04_Entity* value; } *map_b2body_entity;
};



static E04_Entity* entity_create(E04_GameState* state)
{
	if(!(state->entities_alive_count < ENTITY_COUNT))
		// NOTE: this might as well be an assert, if we don't have a way to recover/handle it
		return NULL;

	// // concise version
	//return &state->entities[state->entities_alive_count++];

	E04_Entity* ret = &state->entities[state->entities_alive_count];
	

	ret->alive = true;
	ret->index = state->entities_alive_count;
	++state->entities_alive_count;
	return ret;
}

static void entity_add_physics_body(E04_GameState* state, E04_Entity* entity, b2BodyDef* body_def)
{
	entity->body_id = b2CreateBody(state->world_id, body_def);
	hmput(state->map_b2body_entity, entity->body_id, entity);
}

// NOTE: this only works if nobody holds references to other entities!
//       if that were the case, we couldn't swap them around.
//       We will see in later lectures how to handle this kind of problems
static void entity_destroy(E04_GameState* state, E04_Entity* entity)
{
	// NOTE: here we want to fail hard, nobody should pass us a pointer not gotten from `entity_create()`
	SDL_assert(entity >= state->entities && entity < state->entities + ENTITY_COUNT);

	b2DestroyBody(entity->body_id);
	hmdel(state->map_b2body_entity, entity->body_id);
	entity->alive = false;
}


// game parameters
vec2f design_area_halfsize = vec2f { 5.4f, 2.7f };
float design_shoot_force_min = 1;
float design_shoot_force_max = 10;
float design_shoot_force_dst_min = 0.1;
float design_shoot_force_dst_max = 4;
float design_balls_radius = 0.2f;
float design_balls_restitution = 0.9f;
float design_balls_friction = 1.2f;
float design_balls_velocity_slowdown_threshold = 0.2f;
float design_balls_velocity_slowdown_factor = 0.95;
int design_triangle_side = 5;
float design_holes_radius = 0.1f;
float design_camera_speed = 1.5f;

void debug_design_data(EngineContext* context)
{
	ImGui::Begin("design_data");
	ImGui::PushItemWidth(120);
	ImGui::DragFloat2("area_halfsize", &(design_area_halfsize.x));
	ImGui::DragFloat("shoot_force_min", &design_shoot_force_min);
	ImGui::DragFloat("shoot_force_max", &design_shoot_force_max);
	ImGui::DragFloat("shoot_force_dst_min", &design_shoot_force_dst_min);
	ImGui::DragFloat("shoot_force_dst_max", &design_shoot_force_dst_max);
	ImGui::DragFloat("balls_radius", &design_balls_radius);
	ImGui::DragFloat("balls_restitution", &design_balls_restitution);
	ImGui::DragFloat("balls_friction", &design_balls_friction);
	ImGui::DragFloat("balls_velocity_slowdown_threshold", &design_balls_velocity_slowdown_threshold);
	ImGui::DragFloat("balls_velocity_slowdown_factor", &design_balls_velocity_slowdown_factor);
	ImGui::DragInt("triangle_side", &design_triangle_side);
	ImGui::DragFloat("camera_spped", &design_camera_speed);
	ImGui::PopItemWidth();
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
	

	// texture atlases
	state->atlas = itu_resources_texture_create(context, "data/kenney/simpleSpace_tilesheet_2.png", SDL_SCALEMODE_LINEAR);
	state->bg = itu_resources_texture_create(context, "data/billiard_table.jpg", SDL_SCALEMODE_LINEAR);

}

static void game_reset(EngineContext* context, E04_GameState* state)
{
	// TMP reset uptime (should probably be two different variables
	context->uptime = 0;

	if(b2World_IsValid(state->world_id))
		b2DestroyWorld(state->world_id);
	b2WorldDef def_world = b2DefaultWorldDef();
	def_world.gravity.y = GRAVITY;
	state->world_id = b2CreateWorld(&def_world);
	state->entities_alive_count = 0;
	
	context->camera_active->zoom = 0.2f;
	
	// reset body<>entity map
	hmfree(state->map_b2body_entity);

	// background
	{
		E04_Entity* entity = entity_create(state);
		entity->transform.scale = vec2f { 1.0f, 1.0f };
		itu_lib_sprite_init(&entity->sprite, state->bg, SDL_FRect{0, 0, 1704, 980} );
	}

	// balls
	{
		
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_dynamicBody;
		body_def.linearDamping = 0.2f;
		body_def.angularDamping = 0.9f;

		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.filter.categoryBits = COLLISION_FILTER_BALL;
		shape_def.material.restitution = design_balls_restitution;
		shape_def.material.friction = design_balls_friction;
		shape_def.density = 10;
		shape_def.enableSensorEvents = true;
		
		b2Circle circle = { 0 };
		circle.radius = design_balls_radius;

		
		float row_offset_y = SDL_sqrtf((design_balls_radius * 2)*(design_balls_radius * 2) - design_balls_radius*design_balls_radius);
		int row_size = 1;
		int row_count = design_triangle_side;
		for(int i = 0; i < row_count; ++i)
		{
			float row_offset_x = -design_balls_radius * i;
			for(int j = 0; j < row_size; ++j)
			{
				E04_Entity* entity = entity_create(state);

				entity->transform.scale = vec2f { .5f, .5f };
				itu_lib_sprite_init(&entity->sprite, state->atlas, itu_lib_sprite_get_source_rect(0, 4, context->config.texture_pixels_per_unit, context->config.texture_pixels_per_unit));

				body_def.position = b2Vec2
				{
					i * (row_offset_y) + 2.0f,
					j * (design_balls_radius) * 2 + row_offset_x,
				};
				
				entity_add_physics_body(state, entity, &body_def);
				b2CreateCircleShape(entity->body_id, &shape_def, &circle);
			}
			++row_size;
		}

		// add extra one for flare
		E04_Entity* entity = entity_create(state);
		entity->transform.scale = vec2f { .5f, .5f };
		itu_lib_sprite_init(&entity->sprite, state->atlas, itu_lib_sprite_get_source_rect(0, 4, context->config.texture_pixels_per_unit, context->config.texture_pixels_per_unit));

		body_def.position = b2Vec2 { -2, 0, };
				
		entity_add_physics_body(state, entity, &body_def);
		b2CreateCircleShape(entity->body_id, &shape_def, &circle);
	}

	// walls
	{
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;

		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.filter.categoryBits = COLLISION_FILTER_WALL;

		b2Polygon polygon_lr = b2MakeBox(0.5f, design_area_halfsize.y);
		b2Polygon polygon_tb = b2MakeBox(design_area_halfsize.x, 0.5f);

		for(int i = 0; i < 4; ++i)
		{
			int x, y;
			b2Polygon* wall_shape;

			// compute sides coordinates and choose polygon shape from index
			if(i / 2 == 0)
			{
				// left-right walls
				x = (i % 2) * 2 - 1;
				y = 0;
				wall_shape = &polygon_lr;
			}
			else
			{
				// top-down walls
				x = 0;
				y = (i % 2) * 2 - 1;
				wall_shape = &polygon_tb;
			}

			body_def.position = b2Vec2{ x * (design_area_halfsize.x + 0.5f), y * (design_area_halfsize.y + 0.5f) };

			E04_Entity* entity = entity_create(state);
			entity->transform.scale = VEC2F_ONE;
			itu_lib_sprite_init(&entity->sprite, NULL, { 0 });
			entity_add_physics_body(state, entity, &body_def);
			b2CreatePolygonShape(entity->body_id, &shape_def, wall_shape);
		}
	}

	// holes
	{
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;

		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.filter.categoryBits = COLLISION_FILTER_HOLE;
		shape_def.filter.maskBits = COLLISION_FILTER_BALL;
		shape_def.enableSensorEvents = true;
		shape_def.isSensor = true;

		b2Circle circle = { 0 };
		circle.radius = design_holes_radius;

		for(int i = 0; i < 6; ++i)
		{
			int x = (i % 3)-1;
			int y = (i / 3)*2-1;

			body_def.position = b2Vec2{ x * (design_area_halfsize.x), y * (design_area_halfsize.y) };

			E04_Entity* entity = entity_create(state);
			entity->transform.scale = VEC2F_ONE;

			entity_add_physics_body(state, entity, &body_def);
			b2CreateCircleShape(entity->body_id, &shape_def, &circle);
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

	// small kinematic tweaks to physics
	{
		for(int i = 0; i < BALL_COUNT; ++i)
		{
			E04_BallData* data = &state->balls_data[i];
			if(!data->entity)
				continue;

			E04_Entity* entity = data->entity;
			if(!entity->alive)
				continue;

			// add artifial slowdown to simulate rough surface
			if(length_sq(entity->velocity) < design_balls_velocity_slowdown_threshold*design_balls_velocity_slowdown_threshold)
			{
				entity->velocity = entity->velocity * design_balls_velocity_slowdown_factor;
				b2Body_SetLinearVelocity(entity->body_id, value_cast(b2Vec2, entity->velocity));
				// enable tinting to see precisely when the exponential velocity decay kicks in
				// entity->sprite.tint = COLOR_RED;
			}
			else
				entity->sprite.tint = COLOR_WHITE;
		}
	}

	// input
	{
		E04_PlayerData* data = &state->player_data;
		if(context->btn_isjustpressed[BTN_TYPE_ACTION_0])
		{
			if(data->p0_placed)
			{
				// check if shot hit a target
				b2QueryFilter filter;
				filter.categoryBits = COLLISION_FILTER_PLAYER;
				filter.maskBits = COLLISION_FILTER_BALL;

				vec2f ray_offset = data->p1 - data->p0;
				b2RayResult res = b2World_CastRayClosest(state->world_id, value_cast(b2Vec2, data->p0), value_cast(b2Vec2, ray_offset), filter);

				if(res.hit)
				{
					float t = distance_sq(data->p0, data->p1) / (design_shoot_force_dst_max - design_shoot_force_dst_min);
					vec2f impulse_dir = normalize(data->p1 - data->p0);
					float impulse_strength = (design_shoot_force_max - design_shoot_force_min) * t;
					impulse_strength = SDL_clamp(impulse_strength, design_shoot_force_min, design_shoot_force_max);
					vec2f impulse = impulse_dir * impulse_strength;

					b2BodyId target = b2Shape_GetBody(res.shapeId);
					b2Body_ApplyLinearImpulse(target, value_cast(b2Vec2, impulse), res.point, true);
				}

				data->p0_placed = false;
			}
			else
			{
				data->p0 = itu_lib_context_point_screen_to_global(context, context->mouse_pos);
				data->p0_placed = true;
			}
		}

		if(data->p0_placed)
		{
			data->p1 =  itu_lib_context_point_screen_to_global(context, context->mouse_pos);
		}
	}

	// camera
	{
		if(context->btn_isdown[BTN_TYPE_UP])
			state->camera_pos.y += design_camera_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_DOWN])
			state->camera_pos.y -= design_camera_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_LEFT])
			state->camera_pos.x -= design_camera_speed * context->delta;
		if(context->btn_isdown[BTN_TYPE_RIGHT])
			state->camera_pos.x += design_camera_speed * context->delta;

		const float zoom_speed = 1;
		vec2f camera_offset = vec2f { -1.5f, 0.0f } / context->camera_active->zoom;
		context->camera_active->world_position = state->camera_pos + camera_offset;
		context->camera_active->zoom += context->mouse_scroll * zoom_speed * context->delta;
	}
}

static void game_update_post_physics(EngineContext* context, E04_GameState* state)
{
	// sensor events
	{
		b2SensorEvents sensor_events = b2World_GetSensorEvents(state->world_id);
		for(int i = 0; i < sensor_events.beginCount; ++i)
		{
			b2SensorBeginTouchEvent* sensor_data = &sensor_events.beginEvents[i];
			b2Filter filter_sensor = b2Shape_GetFilter(sensor_data->sensorShapeId);
			b2Filter filter_visitor = b2Shape_GetFilter(sensor_data->visitorShapeId);
			if(filter_sensor.categoryBits == COLLISION_FILTER_HOLE && filter_visitor.categoryBits == COLLISION_FILTER_BALL)
			{
				b2BodyId body_id = b2Shape_GetBody(sensor_data->visitorShapeId);
				E04_Entity* entity = hmget(state->map_b2body_entity, body_id);
				if(!entity)
				{
					SDL_Log("error!");
					continue;
				}
				entity_destroy(state, entity);
			}
		}
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
		if(!entity->alive)
			continue;

		// render texture
		SDL_FRect rect_src = entity->sprite.rect;
		SDL_FRect rect_dst;

		if(DEBUG_render_textures)
			itu_lib_sprite_render(context, &entity->sprite, &entity->transform);

		if(DEBUG_render_outlines)
			itu_lib_sprite_render_debug(context, &entity->sprite, &entity->transform);
	}

	// player aim
	{
		E04_PlayerData* data = &state->player_data;
		if(data->p0_placed)
		{
			itu_lib_render_draw_world_point(context, data->p0, 5, COLOR_YELLOW);
			itu_lib_render_draw_world_line(context, data->p0, data->p1, COLOR_YELLOW);
		}
	}

	if(DEBUG_physics)
		b2World_Draw(state->world_id, &debug_draw);

	// debug window
	itu_lib_render_draw_world_point(context, VEC2F_ZERO, 10, color { 1, 0, 1, 1 });

	SDL_SetRenderDrawColor(context->renderer, 0xFF, 0x00, 0xFF, 0xff);
	SDL_RenderRect(context->renderer, NULL);

	// imgui debug windows
	{
		debug_design_data(context);
	}
}

int main(void)
{
	EngineConfig config = { 0 };
	bool quit = false;
	SDL_Window* window;
	EngineContext context = { 0 };
	E04_GameState  state   = { 0 };

	config.application_name = "ES04.1.2 - Pool Game";
	config.step_per_second_fixed = 60;
	config.texture_pixels_per_unit = 128;
	config.camera_pixel_per_unit   = 128;

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

		SDL_SetRenderDrawColor(context.renderer, 0x00, 0x00, 0x00, 0xFF);
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
