// supported features
// - moving platofrms
// - dashing
// - inclined slopes
// - 3 different types of collision types
//   - COLLISION_TYPE_CONTINOUS   check contact info every frame
//   - COLLISION_TYPE_BEG_END     reacts to contact begin and contact end events
//   - COLLISION_TYPE_RAYCAST     uses a widespread industry trick to bypass 
// 
// some notable possible improvements
// - compute a softer gravity value when steping down a ledge (without jumping)
// - more careful normal conformity tests
//   (right now, we just split the unit circle in 4 to make a perfect square collision test. We might want to allow precise control towards the maximum incline that counts as ground)

#define ENABLE_DIAGNOSTICS

#include <itu_engine.hpp>

const int   ENTITY_COUNT   = 1024;
const int   PLATFORM_COUNT =   32;
const float GRAVITY        =-9.8f;

const int COLLISION_FILTER_PLAYER = 0b00001;
const int COLLISION_FILTER_GROUND = 0b00010;

enum E04_CollisionType
{
	COLLISION_TYPE_CONTINOUS,
	COLLISION_TYPE_BEG_END,
	COLLISION_TYPE_RAYCAST
};
bool DEBUG_render_textures = true;
bool DEBUG_render_outlines = false;
bool DEBUG_physics = true;
int DEBUG_collision_type = COLLISION_TYPE_CONTINOUS;

b2DebugDraw debug_draw;


// design variables
float design_player_hor_speed_ground_max = 7.5f;
float design_player_hor_speed_air_max    = 5.0f;
float design_player_hor_accel_groud = 125.0f;
float design_player_hor_accel_air   = 125.0f;
float design_player_hor_decel_factor_ground = 0.90f;
float design_player_hor_decel_factor_air    = 0.99f;
float design_player_jump_height_max = 3.0f;
float design_player_jump_hor_distance_to_apex_max = 3.0f;
// how much the normal can change from the perfect 4 cardinal coordinates to consider a piece of terrain "ground", "wall" or "ceiling"
// NOTE: not tested with values different from 50%, movement code may need some tweaks to handle collisions that do not fall in the 4 cardina directions
float design_player_surface_normal_diff_threshold = 0.5f;

struct E04_Entity
{
	Sprite      sprite;
	Transform2D transform;
	b2BodyId    body_id;
	vec2f       velocity;
};

struct E04_PlayerData
{
	vec2f normal_ground;
	vec2f velocity_ground;
	vec2f velocity_desired;
	bool is_grounded;
	bool is_colliding_left;
	bool is_colliding_right;
	bool is_colliding_top;
	b2ShapeId colliding_shape_ground;
	b2ShapeId colliding_shape_left;
	b2ShapeId colliding_shape_right;
	b2ShapeId colliding_shape_top;
	float g;   // gravity
};

struct E04_PlatformData
{
	E04_Entity* entity;

	float speed;
	vec2f position_base;
	vec2f position_target_0;
	vec2f position_target_1;
};

struct E04_GameState
{
	// shortcut references
	E04_Entity* player;
	
	// game-allocated memory
	E04_Entity* entities;
	int entities_alive_count;
	E04_PlayerData player_data;
	E04_PlatformData platform_data[PLATFORM_COUNT];

	// SDL-allocated structures
	SDL_Texture* atlas;

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
	++state->entities_alive_count;
	return ret;
}

static void entity_add_physics_body(E04_GameState* state, E04_Entity* entity, b2BodyDef* body_def)
{
	entity->body_id = b2CreateBody(state->world_id, body_def);
	stbds_hmput(state->map_b2body_entity,entity->body_id,entity);
}

static E04_Entity* entity_get_from_body(E04_GameState* state, b2BodyId body_id)
{
	return stbds_hmget(state->map_b2body_entity,body_id);
}

void debug_ui_player_data(E04_GameState* state)
{
	E04_Entity* entity = state->player;
	E04_PlayerData* data = &state->player_data;

	ImGui::Begin("game_player");
	ImGui::PushItemWidth(120);

	ImGui::CollapsingHeader("Player (design)", ImGuiTreeNodeFlags_Leaf);
	
	ImGui::DragFloat("hor_speed_ground_max", &design_player_hor_speed_ground_max);
	ImGui::DragFloat("hor_speed_air_max", &design_player_hor_speed_air_max);
	ImGui::DragFloat("hor_accel_groud", &design_player_hor_accel_groud);
	ImGui::DragFloat("hor_accel_air", &design_player_hor_accel_air);
	ImGui::DragFloat("hor_decel_factor_ground", &design_player_hor_decel_factor_ground);
	ImGui::DragFloat("hor_decel_factor_air", &design_player_hor_decel_factor_air);
	ImGui::DragFloat("jump_height_max", &design_player_jump_height_max);
	ImGui::DragFloat("jump_hor_distance_to_apex_max", &design_player_jump_hor_distance_to_apex_max);

	ImGui::CollapsingHeader("Player (Runtime)", ImGuiTreeNodeFlags_Leaf);
	ImGui::LabelText("position", "%4.2f   %4.2f", entity->transform.position.x, entity->transform.position.y);
	ImGui::LabelText("velocity",  "%4.2f   %4.2f", entity->velocity.x, entity->velocity.y);
	ImGui::LabelText("gravity", "%4.2f", data->g);
	ImGui::LabelText("grounded", "%s", data->is_grounded ? "X" : "O");
	ImGui::LabelText("Lcollide", "%s", data->is_colliding_left ? "X" : "O");
	ImGui::LabelText("Rcollide", "%s", data->is_colliding_right ? "X" : "O");
	ImGui::LabelText("Tcollide", "%s", data->is_colliding_top ? "X" : "O");
	ImGui::Separator();
	ImGui::LabelText("velocity desired",  "%4.2f   %4.2f", data->velocity_desired.x, data->velocity_desired.y);
	ImGui::LabelText("velocity ground" ,  "%4.2f   %4.2f", data->velocity_ground.x, data->velocity_ground.y);

	ImGui::PopItemWidth();
	ImGui::End();
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

void compute_jump_parameters(E04_Entity* entity, float* out_vertical_speed, float* out_gravity)
{
	float v_x = design_player_hor_speed_ground_max;
	float h = design_player_jump_height_max;
	float x_h = design_player_jump_hor_distance_to_apex_max;
	*out_vertical_speed = (2 * h * v_x) / x_h;
	*out_gravity        = (-2 * h * v_x*v_x) / (x_h*x_h);
}

static void add_terrain_piece(b2BodyId id, vec2f halfsize, vec2f position, float rotation)
{
	b2ShapeDef shape_def = b2DefaultShapeDef();
	shape_def.filter.categoryBits = COLLISION_FILTER_GROUND;

	b2Polygon polygon = b2MakeOffsetBox(halfsize.x, halfsize.y, value_cast(b2Vec2, position), b2MakeRot(rotation));

	b2CreatePolygonShape(id, &shape_def, &polygon);
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
	itu_lib_input_set_mapping_keyboard(context, SDLK_A, BTN_TYPE_LEFT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_D, BTN_TYPE_RIGHT);
	itu_lib_input_set_mapping_keyboard(context, SDLK_SPACE, BTN_TYPE_SPACE);

	itu_lib_input_set_mapping_keyboard(context, SDLK_F1, BTN_TYPE_DEBUG_F1);
	itu_lib_input_set_mapping_keyboard(context, SDLK_TAB, BTN_TYPE_DEBUG_RESET);

	// allocate memory
	state->entities = (E04_Entity*)SDL_calloc(ENTITY_COUNT, sizeof(E04_Entity));
	SDL_assert(state->entities);

	state->world_id = { 0 };
	

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
	stbds_hmfree(state->map_b2body_entity);

	// player
	{
		E04_Entity* entity = entity_create(state);
		state->player = entity;
		state->player_data = { 0 };
		state->player_data.g = -66.67f;


		entity->transform.position = VEC2F_ONE;
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
			body_def.position = b2Vec2{ 0, 1 };
			body_def.name = "player";
			body_def.gravityScale = 0;
			body_def.sleepThreshold = 0.1f;

			b2ShapeDef shape_def = b2DefaultShapeDef();
			shape_def.density = 1; // NOTE: default density of 0 will mess with collisions and gravity!
			shape_def.material.friction = 0.0f;
			shape_def.enableSensorEvents  = true;
			shape_def.enableContactEvents = true;
			shape_def.enableHitEvents     = true;
			shape_def.filter.categoryBits = COLLISION_FILTER_PLAYER;
			shape_def.filter.maskBits = COLLISION_FILTER_GROUND;
			
			b2Circle circle;
			circle.radius = 0.5f;
			circle.center = value_cast(b2Vec2, offset);

			entity_add_physics_body(state, entity, &body_def);
			b2CreateCircleShape(entity->body_id, &shape_def, &circle);
		}
	}

	// terrain
	{
		
		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_staticBody;
		body_def.name = "terrain";
		E04_Entity* entity = entity_create(state);
		
		entity_add_physics_body(state, entity, &body_def);
		add_terrain_piece(entity->body_id, vec2f { 32, 0.5f }, vec2f { 0, -0.5f }, 0); // ground
		add_terrain_piece(entity->body_id, vec2f { 3, 0.5f }, vec2f { -7, 3 }, PI_HALF); // wall
		add_terrain_piece(entity->body_id, vec2f { 3, 0.5f }, vec2f { -9, 4 }, 0); // ceiling
		add_terrain_piece(entity->body_id, vec2f { 3, 0.5f }, vec2f { -5.5f, 1.0f }, -PI_HALF/3); // incline
	}

	// platforms
	{
		SDL_memset(state->platform_data, 0, sizeof(E04_PlatformData) * PLATFORM_COUNT);

		b2BodyDef body_def = b2DefaultBodyDef();
		body_def.type = b2_kinematicBody;
		b2ShapeDef shape_def = b2DefaultShapeDef();
		shape_def.filter.categoryBits = COLLISION_FILTER_GROUND;

		b2Polygon polygon = b2MakeBox(2.0f, 0.25f);

		vec2f spacing        = vec2f { 3, 2.5f };
		vec2f base_pos_group = vec2f { 3, 1.5f };
		vec2f target_offset  = vec2f { 3, 0 };
		for(int i = 0; i < 6; ++i)
		{
			
			E04_Entity* entity = entity_create(state);
			E04_PlatformData* data = &state->platform_data[i];

			int x = i % 2;
			int y = i;

			vec2f base_pos = base_pos_group;
			base_pos.x += spacing.x * x;
			base_pos.y += spacing.y * y;

			body_def.position = value_cast(b2Vec2, base_pos);

			entity_add_physics_body(state, entity, &body_def);
			b2CreatePolygonShape(entity->body_id, &shape_def, &polygon);
			

			vec2f offset = target_offset * (i % 2 == 0 ? -1 : 1);

			float TMP_platform_speed = 5;

			data->entity = entity;
			data->speed = TMP_platform_speed;
			data->position_base = base_pos;
			data->position_target_0 = base_pos + offset;
			data->position_target_1 = base_pos - offset;

			entity->velocity = normalize(offset) * TMP_platform_speed;
			int a = 0;
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

	// player
	{
		E04_Entity* entity = state->player;
		E04_PlayerData* data = &state->player_data;
		vec2f velocity = data->velocity_desired;

		// manual collision detection through raycast
		// NOTE: this can be helpful to smooth out some small engine finnickiness, or to cover for sub-optimal physics<>update synchronization
		//       BUT it is kinda costly, especially when done in the normal update (which might run much faster than the fixed physics loop)
		//       Nonetheless it can be very useful, especially for entities controlled by the player which are inherently less predictable than NPCs and such
		if(DEBUG_collision_type == COLLISION_TYPE_RAYCAST)
		{
			b2QueryFilter query_filter_player_ground = { 0 };
			query_filter_player_ground.categoryBits = COLLISION_FILTER_PLAYER;
			query_filter_player_ground.maskBits = COLLISION_FILTER_GROUND;

			float TMP_player_radius = 0.5f;
			float TMP_player_raycast_distance_ground = TMP_player_radius + 0.1f;
			float TMP_player_raycast_distance = TMP_player_radius + 0.05f;

			vec2f raycast_pos = entity->transform.position;
			raycast_pos.y += 0.5f;

			b2RayResult res_down = b2World_CastRayClosest(state->world_id, value_cast(b2Vec2, raycast_pos), b2Vec2 { 0, -TMP_player_raycast_distance_ground }, query_filter_player_ground);
			data->is_grounded = res_down.hit;
			if(res_down.hit)
			{
				data->normal_ground = value_cast(vec2f, res_down.normal);
				b2BodyId id_ground = b2Shape_GetBody(res_down.shapeId);
				b2Vec2 velocity_ground = b2Body_GetLinearVelocity(id_ground);
				data->velocity_ground = value_cast(vec2f, velocity_ground);
			}

			b2RayResult res_top = b2World_CastRayClosest(state->world_id, value_cast(b2Vec2, raycast_pos), b2Vec2 { 0, TMP_player_raycast_distance }, query_filter_player_ground);
			b2RayResult res_l = b2World_CastRayClosest(state->world_id, value_cast(b2Vec2, raycast_pos), b2Vec2 { -TMP_player_raycast_distance, 0 }, query_filter_player_ground);
			b2RayResult res_r = b2World_CastRayClosest(state->world_id, value_cast(b2Vec2, raycast_pos), b2Vec2 {  TMP_player_raycast_distance, 0 }, query_filter_player_ground);
			data->is_colliding_top = res_top.hit;
			data->is_colliding_left = res_l.hit;
			data->is_colliding_right = res_r.hit;
		}

		float hor_accel;
		float hor_decel_factor;
		float hor_speed_max;
		if(data->is_grounded)
		{
			hor_accel = design_player_hor_accel_groud;
			hor_decel_factor = design_player_hor_decel_factor_ground;
			hor_speed_max = design_player_hor_speed_ground_max;
		}
		else
		{
			hor_accel = design_player_hor_accel_air;
			hor_decel_factor = design_player_hor_decel_factor_air;
			hor_speed_max = design_player_hor_speed_air_max;
		}

		velocity.x = SDL_clamp(velocity.x, -hor_speed_max, hor_speed_max);

		if(data->is_grounded)
		{
			
			if(context->btn_isdown[BTN_TYPE_LEFT])
				velocity += cross_triplet(data->normal_ground, VEC2F_LEFT, data->normal_ground) * hor_accel * context->delta;
			else if(context->btn_isdown[BTN_TYPE_RIGHT])
				velocity += cross_triplet(data->normal_ground, VEC2F_RIGHT, data->normal_ground) * hor_accel * context->delta;
			else
				velocity = velocity * hor_decel_factor;
		
#if defined PLAYER_COLLISION_RAYCAST
			velocity.x = SDL_clamp(velocity.x, -hor_speed_max, hor_speed_max);
#else
			velocity = clamp(velocity, hor_speed_max);
#endif

			if(data->is_colliding_left && velocity.x < 0)
				velocity = VEC2F_ZERO;
			if(data->is_colliding_right && velocity.x > 0)
				velocity = VEC2F_ZERO;

			if(context->btn_isjustpressed[BTN_TYPE_SPACE])
			{
				compute_jump_parameters(entity, &velocity.y, &data->g);
			}

		}
		else
		{
			
			if(data->is_colliding_top)
				velocity.y = SDL_min(velocity.y, 0);

			if(context->btn_isdown[BTN_TYPE_LEFT])
				velocity.x -= hor_accel * context->delta;
			else if(context->btn_isdown[BTN_TYPE_RIGHT])
				velocity.x += hor_accel * context->delta;
			else
				velocity.x *= hor_decel_factor;

			if(velocity.y > 0)
				velocity.y += state->player_data.g * context->delta;
			else
				velocity.y += state->player_data.g * context->delta * 3;
		}
		
		itu_lib_render_draw_world_line(context, state->player->transform.position, state->player->transform.position + normalize(velocity), COLOR_GREEN);

		data->velocity_desired = velocity;
		vec2f velocity_total = velocity + data->velocity_ground;

		float TMP_player_speed_threshold = 0.1f;
		if(length_sq(velocity_total) < TMP_player_speed_threshold)
			velocity_total = VEC2F_ZERO;
		b2Body_SetLinearVelocity(entity->body_id, value_cast(b2Vec2, velocity_total));
	}

	// platforms
	{
		for(int i = 0; i < PLATFORM_COUNT; ++i)
		{
			E04_PlatformData* data = &state->platform_data[i];
			E04_Entity* entity = data->entity;

			if(!entity)
				continue;

			vec2f dir = data->position_target_0 - entity->transform.position;
			if(dot(dir, entity->velocity) < 0)
			{
				// flip target and base
				entity->velocity = -normalize(entity->velocity) * data->speed;
				vec2f swap = data->position_target_0;
				data->position_target_0 = data->position_target_1;
				data->position_target_1 = swap;
			}

			b2Body_SetLinearVelocity(entity->body_id, value_cast(b2Vec2, entity->velocity)); 
		}
	}

}

static void game_update_post_physics(EngineContext* context, E04_GameState* state)
{
	// player
	if(DEBUG_collision_type == COLLISION_TYPE_CONTINOUS)
	{
		E04_Entity* entity = state->player;
		E04_PlayerData* data = &state->player_data;
	
		
		static b2ContactData* contact_data = (b2ContactData*)SDL_calloc(context->config.physics_contacts_per_entity_max, sizeof(b2ContactData));
	
		int contacts = b2Body_GetContactCapacity(entity->body_id);
		SDL_assert(contacts <= context->config.physics_contacts_per_entity_max && "Max number of contacts exceeded. If this is not an error, increase EngineConfig::physics_contacts_per_entity_max");

		int actual_contacts = b2Body_GetContactData(state->player->body_id, contact_data, contacts);

		// in this mode we are re-checking all collisions every frame, so we need to reset this all the time
		// (in a real game we woul probably like to "buffer" at least the ground: pretend we are still colliding for a few frames so that the games feels less finnicky)
		data->is_grounded        = false;
		data->is_colliding_top   = false;
		data->is_colliding_left  = false;
		data->is_colliding_right = false;
		for(int i = 0; i < actual_contacts; ++i)
		{
			b2ContactData* contact = &contact_data[i];

			// // code below works under the assumption that we stop having contacts immediately when we stop, which is not always true
			// // (ie, sometime contact persists for an additional frame after jumping). Doing it only when the point was the first contact solves the issue
			// // NOTE: player is a circle so we can assume that we have always a single contact point
			// if(contact->manifold.points[0].persisted)
			// 	continue;

			b2Filter filter_a = b2Shape_GetFilter(contact->shapeIdA);
			vec2f collision_normal = value_cast(vec2f, contact->manifold.normal);

			// NOTE: normal points AWAY from the surface, so we need to cehck with the opposite vector
			float normal_diff_ground  = dot(VEC2F_UP, collision_normal);
			float normal_diff_ceiling = dot(VEC2F_DOWN, collision_normal);
			float normal_diff_left    = dot(VEC2F_RIGHT, collision_normal);
			float normal_diff_right   = dot(VEC2F_LEFT, collision_normal);

			// TMP debug names
			const char * TMP_name_a;
			const char * TMP_name_b;
			{
				b2BodyId id_a = b2Shape_GetBody(contact->shapeIdA);
				b2BodyId id_b = b2Shape_GetBody(contact->shapeIdB);
					
				TMP_name_a = b2Body_GetName(id_a);
				TMP_name_b = b2Body_GetName(id_b);
			}
				
			if(filter_a.categoryBits & COLLISION_FILTER_GROUND)
			{
				if(normal_diff_ground > design_player_surface_normal_diff_threshold)
				{
					// TMP velocity should come from entity, not b2Body
					b2BodyId other_id = b2Shape_GetBody(contact->shapeIdA);
					b2BodyId my_id    = b2Shape_GetBody(contact->shapeIdB);
					b2Vec2 other_velocity = b2Body_GetLinearVelocity(other_id);
					b2Vec2 my_velocity    = b2Body_GetLinearVelocity(my_id);

					float TMP_grouded_velocity_threshold = 4;
					// NOTE: adding an extra safety check because sometimes we still detect collision with the ground for one frame after jumping
					//       It's probably that this is a mistake on our side, but investigating it would take time I don't have.
					//       What we're doing instead is saying "we can't be grounded if our vertical velocity is pushing us upwards"
					//       Normally just `<= 0` would do, but since the game has additional custom logic for inclined slopes we need to increase the threshold to keep into
					//       account climbing those. The threshold must be bigger than the velocity on the highest possible slope inclide, but smaller than the minimim jump initial vertical velocity
					if(my_velocity.y <= TMP_grouded_velocity_threshold)
					{
						data->is_grounded = true;
						data->velocity_ground = value_cast(vec2f, other_velocity);
						data->normal_ground = value_cast(vec2f, collision_normal);
					}
				}
				else if(normal_diff_ceiling > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_top = true;
				}
				else if(normal_diff_left > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_left = true;
				}
				else if(normal_diff_right > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_right = true;
				}

				// // alternative, more coincisew way to do the same as above (especially common in older codebases)
				// data->is_grounded        |= normal_diff_ground  > design_player_surface_normal_diff_threshold;
				// data->is_colliding_top   |= normal_diff_ceiling > design_player_surface_normal_diff_threshold;
				// data->is_colliding_left  |= normal_diff_left    > design_player_surface_normal_diff_threshold;
				// data->is_colliding_right |= normal_diff_right   > design_player_surface_normal_diff_threshold;
			}
		}
	}
	else if(DEBUG_collision_type == COLLISION_TYPE_BEG_END)
	{
		// collision logic handled with begin and contact events
		b2ContactEvents world_contact_events = b2World_GetContactEvents(state->world_id);
	
		for(int i = 0; i < world_contact_events.endCount; ++i)
		{
			b2ContactEndTouchEvent* event = &world_contact_events.endEvents[i];

			b2Filter filter_a = b2Shape_GetFilter(event->shapeIdA);
			b2Filter filter_b = b2Shape_GetFilter(event->shapeIdB);

			if(!(filter_b.categoryBits & COLLISION_FILTER_PLAYER))
				continue;

			b2BodyId id_a = b2Shape_GetBody(event->shapeIdA);
			b2BodyId id_b = b2Shape_GetBody(event->shapeIdB);

			E04_Entity* entity = entity_get_from_body(state, id_b);
			E04_PlayerData* data = &state->player_data;

			const char* TMP_name_a = b2Body_GetName(id_a);
			const char* TMP_name_b = b2Body_GetName(id_b);


			if(filter_a.categoryBits & COLLISION_FILTER_GROUND)
			{
				if(data->is_grounded && B2_ID_EQUALS(event->shapeIdA, data->colliding_shape_ground))
				{
					data->is_grounded = false;
				}
				else if(data->is_colliding_top && B2_ID_EQUALS(event->shapeIdA, data->colliding_shape_top))
				{
					data->is_colliding_top = false;
				}
				else if(data->is_colliding_left && B2_ID_EQUALS(event->shapeIdA, data->colliding_shape_left))
				{
					data->is_colliding_left = false;
				}
				else if(data->is_colliding_right && B2_ID_EQUALS(event->shapeIdA, data->colliding_shape_right))
				{
					data->is_colliding_right = false;
				}
				else
					SDL_Log("WARNING surface does not conform to correct surface orientation");
			}
		}

		for(int i = 0; i < world_contact_events.beginCount; ++i)
		{
			b2ContactBeginTouchEvent* event = &world_contact_events.beginEvents[i];
			b2Filter filter_a = b2Shape_GetFilter(event->shapeIdA);
			b2Filter filter_b = b2Shape_GetFilter(event->shapeIdB);

			if(!(filter_b.categoryBits & COLLISION_FILTER_PLAYER))
				continue;

			b2BodyId id_a = b2Shape_GetBody(event->shapeIdA);
			b2BodyId id_b = b2Shape_GetBody(event->shapeIdB);

			E04_Entity* entity = entity_get_from_body(state, id_b);
			E04_PlayerData* data = &state->player_data;

			const char* TMP_name_a = b2Body_GetName(id_a);
			const char* TMP_name_b = b2Body_GetName(id_b);

			vec2f collision_normal = value_cast(vec2f, event->manifold.normal);

			// NOTE: normal points AWAY from the surface, so we need to cehck with the opposite vector
			float normal_diff_ground  = dot(VEC2F_UP, collision_normal);
			float normal_diff_ceiling = dot(VEC2F_DOWN, collision_normal);
			float normal_diff_left    = dot(VEC2F_RIGHT, collision_normal);
			float normal_diff_right   = dot(VEC2F_LEFT, collision_normal);

			if(filter_a.categoryBits & COLLISION_FILTER_GROUND)
			{
				if(normal_diff_ground > design_player_surface_normal_diff_threshold)
				{
					data->is_grounded = true;
					data->colliding_shape_ground = event->shapeIdA;
					data->normal_ground = value_cast(vec2f, collision_normal);
				}
				else if(normal_diff_ceiling > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_top = true;
					data->colliding_shape_top = event->shapeIdA;
				}
				else if(normal_diff_left > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_left = true;
					data->colliding_shape_left = event->shapeIdA;
				}
				else if(normal_diff_right > design_player_surface_normal_diff_threshold)
				{
					data->is_colliding_right = true;
					data->colliding_shape_right = event->shapeIdA;
				}
				else
					SDL_Log("WARNING surface does not conform to correct surface orientation");
			}
		}
	}

	// camera
	{
		const float zoom_speed = 1;
		vec2f camera_offset = vec2f { -5.0f, 3.0f } / context->camera_active->zoom;
		// camera follows player
		context->camera_active->world_position = state->player->transform.position + camera_offset;
		context->camera_active->zoom += context->mouse_scroll * zoom_speed * context->delta;
	}
}

static void game_render(EngineContext* context, E04_GameState* state)
{
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

	debug_ui_player_data(state);
	
	itu_lib_render_draw_world_line(context, state->player->transform.position, state->player->transform.position + state->player_data.normal_ground, COLOR_RED);

	// debug window
	itu_lib_render_draw_world_point(context, VEC2F_ZERO, 10, color { 1, 0, 1, 1 });

	SDL_SetRenderDrawColor(context->renderer, 0xFF, 0x00, 0xFF, 0xff);
	SDL_RenderRect(context->renderer, NULL);
}

int main(void)
{
	EngineConfig config = { 0 };
	bool quit = false;
	SDL_Window* window;
	EngineContext context = { 0 };
	E04_GameState  state   = { 0 };

	config.application_name = "ES04.1.3 - Platformer Game";
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
