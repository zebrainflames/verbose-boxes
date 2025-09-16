#include "math_functions.h"
#include "mruby.h"
#include "mruby/value.h"
#include <assert.h>
#include <dragonruby.h>
#include <mruby/array.h>
#include <mruby/data.h>
#include <mruby/proc.h>
#include <mruby/variable.h>
#include <string.h>

// testing box2d includes:
#include "box2d.h"
#include "id.h"
#include "types.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static drb_api_t *drb_api;

// Screen bounds are currently just the DragonRuby defaults
static float g_screen_width = 1280.0f;
static float g_screen_height = 720.0f;

static const float PIXELS_PER_METER = 32.0f;

static b2WorldDef mainWorldDef;

typedef struct {
	mrb_value value;
} mrb_value_holder;

static void b2WorldId_free(mrb_state *mrb, void *p) {
	printf("[CExt] -- INFO: freeing Box2D world");
	b2WorldId *id = (b2WorldId *)p;
	b2DestroyWorld(*id);
	*id = b2_nullWorldId;
	mrb_free(mrb, p);
}

static const struct mrb_data_type b2WorldId_type = {
	"b2WorldId",
	b2WorldId_free,
};

static void b2BodyId_free(mrb_state *mrb, void *p) {
	b2BodyId *bodyId = (b2BodyId *)p;
	mrb_value_holder *holder = (mrb_value_holder *)b2Body_GetUserData(*bodyId);
	if (holder) {
		mrb_free(mrb, holder);
	}
	b2DestroyBody(*(b2BodyId *)p);
	mrb_free(mrb, p);
}

static const struct mrb_data_type b2BodyId_type = {
	"b2BodyId",
	b2BodyId_free,
};

static b2Vec2 pixels_to_meters(float x, float y) { return (b2Vec2){x / PIXELS_PER_METER, y / PIXELS_PER_METER}; }

static b2Vec2 meters_to_pixels(float x, float y) { return (b2Vec2){x * PIXELS_PER_METER, y * PIXELS_PER_METER}; }

static mrb_value world_initialize(mrb_state *mrb, mrb_value self) {
	mrb_float width, height;
	mrb_get_args(mrb, "ff", &width, &height);
	g_screen_width = width;
	g_screen_height = height;

	b2SetLengthUnitsPerMeter(PIXELS_PER_METER);
	mainWorldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&mainWorldDef);
	b2World_SetGravity(worldId, (b2Vec2){0.0f, -9.8f});

	b2WorldId *worldId_ptr = (b2WorldId *)mrb_malloc(mrb, sizeof(b2WorldId));
	*worldId_ptr = worldId;

	mrb_data_init(self, worldId_ptr, &b2WorldId_type);

	return self;
}

static mrb_value world_create_body(mrb_state *mrb, mrb_value self) {
	b2WorldId *worldId = DATA_PTR(self);
	//printf("[CExt] -- INFO: Creating Body...\n");
	mrb_value type_str;
	mrb_float x, y;
	mrb_bool allow_sleep = true;
	mrb_get_args(mrb, "Sff|b", &type_str, &x, &y, &allow_sleep);

	struct RClass *module = drb_api->mrb_module_get(mrb, "FFI");
	module = drb_api->mrb_module_get_under(mrb, module, "Box2D");
	struct RClass *body_class = drb_api->mrb_class_get_under(mrb, module, "Body");
	mrb_value body_obj = drb_api->mrb_obj_new(mrb, body_class, 0, NULL);

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.position = pixels_to_meters(x, y);

	mrb_value_holder *holder = (mrb_value_holder *)mrb_malloc(mrb, sizeof(mrb_value_holder));
	holder->value = body_obj;
	bodyDef.userData = holder;

	b2BodyType type = b2_staticBody;
	if (strcmp(mrb_str_to_cstr(mrb, type_str), "dynamic") == 0) {
		//printf("Creating dynamic body\n");
		type = b2_dynamicBody;
		bodyDef.linearDamping = 0.0f;
		bodyDef.enableSleep = allow_sleep;
	} else if (strcmp(mrb_str_to_cstr(mrb, type_str), "kinematic") == 0) {
		//printf("Creating static body\n");
		type = b2_kinematicBody;
	}


	bodyDef.type = type;
	b2BodyId bodyId = b2CreateBody(*worldId, &bodyDef);

	b2BodyId *bodyId_ptr = (b2BodyId *)mrb_malloc(mrb, sizeof(b2BodyId));
	*bodyId_ptr = bodyId;

	mrb_data_init(body_obj, bodyId_ptr, &b2BodyId_type);

	return body_obj;
}

static mrb_value body_create_box_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float width, height, density;
	mrb_bool enable_contacts = false; // Default to false
	mrb_get_args(mrb, "fff|b", &width, &height, &density, &enable_contacts);

	assert(width > 0.0f && "width cannot be zero or negative");
	assert(height > 0.0f && "height cannot be zero or negative");

	b2Vec2 half_extents_meters = (b2Vec2){width / PIXELS_PER_METER / 2.0f, height / PIXELS_PER_METER / 2.0f};
	b2Polygon box = b2MakeBox(half_extents_meters.x, half_extents_meters.y);
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.enableContactEvents = enable_contacts;
	b2CreatePolygonShape(*bodyId, &shapeDef, &box);

	return mrb_nil_value();
}

// Helper function to create an offset polygon for a box using b2MakeBox and translation
// This is used to easily create the tetriminos
static void create_offset_box_fixture(
	b2BodyId bodyId, 
	const b2ShapeDef *shapeDef, 
	float box_width_px, float box_height_px,
	float offset_x_px, float offset_y_px) {
	float hw_m = (box_width_px / PIXELS_PER_METER) / 2.0f;
	float hh_m = (box_height_px / PIXELS_PER_METER) / 2.0f;
	float offset_x_m = offset_x_px / PIXELS_PER_METER;
	float offset_y_m = offset_y_px / PIXELS_PER_METER;

	b2Polygon poly = b2MakeBox(hw_m, hh_m);
	b2Vec2 offset = {offset_x_m, offset_y_m};
	for (int i = 0; i < poly.count; ++i) {
		poly.vertices[i] = b2Add(poly.vertices[i], offset);
	}
	poly.centroid = b2Add(poly.centroid, offset);

	b2CreatePolygonShape(bodyId, shapeDef, &poly);
}

// T-shape. Origin is the center of the 3-block horizontal bar.
//   #
// # # #
static mrb_value body_create_t_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_get_args(mrb, "ff", &square_size_px, &density);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.enableContactEvents = false;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, square_size_px);

	return mrb_nil_value();
}

// O-shape (2x2 box). Origin is the center of the 4 blocks.
// ##
// ##
static mrb_value body_create_box_shape_2x2(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_get_args(mrb, "ff", &square_size_px, &density);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.enableContactEvents = false;

	
	float s_half = square_size_px / 2.0f;
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -s_half, -s_half);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, s_half, -s_half);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -s_half, s_half);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, s_half, s_half);

	return mrb_nil_value();
}

// L-shape. Origin is the center of the 3-block segment.
//   #
//   #
// ###
static mrb_value body_create_l_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_get_args(mrb, "ff", &square_size_px, &density);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.enableContactEvents = false;

	
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, square_size_px);

	return mrb_nil_value();
}

// J-shape (mirrored L). Origin is the center of the 3-block segment.
// #
// #
// ###
static mrb_value body_create_j_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_get_args(mrb, "ff", &square_size_px, &density);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.enableContactEvents = false;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, square_size_px);

	return mrb_nil_value();
}

static mrb_value world_step(mrb_state *mrb, mrb_value self) {
	b2WorldId *worldId = DATA_PTR(self);

	mrb_float time_step;
	// TODO: do we need to provide time step here?
	mrb_get_args(mrb, "f", &time_step);

	// TODO: what is a good sub step range?
	b2World_Step(*worldId, time_step, 8);

	b2ContactEvents events = b2World_GetContactEvents(*worldId);
	for (int i = 0; i < events.beginCount; ++i) {
		b2ContactBeginTouchEvent event = events.beginEvents[i];
		b2BodyId bodyIdA = b2Shape_GetBody(event.shapeIdA);
		b2BodyId bodyIdB = b2Shape_GetBody(event.shapeIdB);

		mrb_value_holder *holderA = (mrb_value_holder *)b2Body_GetUserData(bodyIdA);
		mrb_value_holder *holderB = (mrb_value_holder *)b2Body_GetUserData(bodyIdB);

		if (holderA && holderB) {
			mrb_value ruby_body_a = holderA->value;
			mrb_value ruby_body_b = holderB->value;

			mrb_value contacts_a = mrb_iv_get(mrb, ruby_body_a, mrb_intern_lit(mrb, "@contacts"));
			if (mrb_nil_p(contacts_a)) {
				contacts_a = mrb_ary_new(mrb);
				mrb_iv_set(mrb, ruby_body_a, mrb_intern_lit(mrb, "@contacts"), contacts_a);
			}
			mrb_value b_id = mrb_funcall(mrb, ruby_body_b, "object_id", 0);
			mrb_ary_push(mrb, contacts_a, b_id);

			mrb_value contacts_b = mrb_iv_get(mrb, ruby_body_b, mrb_intern_lit(mrb, "@contacts"));
			if (mrb_nil_p(contacts_b)) {
				contacts_b = mrb_ary_new(mrb);
				mrb_iv_set(mrb, ruby_body_b, mrb_intern_lit(mrb, "@contacts"), contacts_b);
			}
			mrb_value a_id = mrb_funcall(mrb, ruby_body_a, "object_id", 0);
			mrb_ary_push(mrb, contacts_b, a_id);
		}
	}

	return mrb_nil_value();
}

static mrb_value body_get_info(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	b2Vec2 pos = b2Body_GetPosition(*bodyId);
	b2Vec2 vel = b2Body_GetLinearVelocity(*bodyId);
	bool awake = b2Body_IsAwake(*bodyId);
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "pos=(%f, %f), vel=(%f, %f), awake=%s", pos.x, pos.y, vel.x, vel.y, awake ? "true" : "false");
	return drb_api->mrb_str_new_cstr(mrb, buffer);
}

static mrb_value body_is_awake(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	return mrb_bool_value(b2Body_IsAwake(*bodyId));
}

static mrb_value body_position(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	b2Vec2 position = b2Body_GetPosition(*bodyId);
	position = meters_to_pixels(position.x, position.y);

	mrb_value hash = drb_api->mrb_hash_new(mrb);
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x")),
						  drb_api->mrb_float_value(mrb, position.x));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y")),
						  drb_api->drb_float_value(mrb, position.y));

	return hash;
}

static mrb_value body_position_meters(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	b2Vec2 position = b2Body_GetPosition(*bodyId); // Raw Box2D meter coords

	mrb_value hash = drb_api->mrb_hash_new(mrb);
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x")),
						  drb_api->mrb_float_value(mrb, position.x));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y")),
						  drb_api->drb_float_value(mrb, position.y));

	return hash;
}

static mrb_value body_extents(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	b2ShapeId shapeIds[1]; // Assuming only one shape per body for now
	int shapeCount = b2Body_GetShapes(*bodyId, shapeIds, 1);
	if (shapeCount == 0) {
		return mrb_nil_value(); // No shapes found, return nil
	}
	b2ShapeId shapeId = shapeIds[0];

	if (b2Shape_GetType(shapeId) == b2_polygonShape) {
		b2Polygon polygon = b2Shape_GetPolygon(shapeId);
		// Calculate width and height from the polygon's vertices
		// Assuming a box created by b2MakeBox, vertices are at (-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)
		float width_meters = polygon.vertices[1].x - polygon.vertices[0].x; // (halfWidth - (-halfWidth)) = 2 * halfWidth
		float height_meters = polygon.vertices[2].y - polygon.vertices[1].y; // (halfHeight - (-halfHeight)) = 2 * halfHeight

		mrb_value hash = drb_api->mrb_hash_new(mrb);
		drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "w")),
							  drb_api->drb_float_value(mrb, width_meters * PIXELS_PER_METER));
		drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "h")),
							  drb_api->drb_float_value(mrb, height_meters * PIXELS_PER_METER));
		return hash;
	}

	return mrb_nil_value();
}

static mrb_value body_get_shapes_info(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	// First, get the count of shapes
	int shapeCount = b2Body_GetShapeCount(*bodyId);
	if (shapeCount == 0) {
		return mrb_ary_new(mrb); // NOTE: should probably just return nil here!
	}

	// Allocate memory to hold the shape IDs
	b2ShapeId *shapeIds = mrb_malloc(mrb, sizeof(b2ShapeId) * shapeCount); // TODO: remove silly malloc
	b2Body_GetShapes(*bodyId, shapeIds, shapeCount);

	mrb_value result_array = mrb_ary_new_capa(mrb, shapeCount);

	for (int i = 0; i < shapeCount; i++) {
		b2ShapeId shapeId = shapeIds[i];
		if (b2Shape_GetType(shapeId) == b2_polygonShape) {
			b2Polygon polygon = b2Shape_GetPolygon(shapeId);

			// The polygon's centroid is its center relative to the body's origin (in meters)
			b2Vec2 center_meters = polygon.centroid;

			// Manually compute the AABB from the vertices to find width and height
			b2Vec2 min_v = polygon.vertices[0];
			b2Vec2 max_v = polygon.vertices[0];
			for (int j = 1; j < polygon.count; ++j) {
				b2Vec2 v = polygon.vertices[j];
				min_v.x = fminf(min_v.x, v.x);
				min_v.y = fminf(min_v.y, v.y);
				max_v.x = fmaxf(max_v.x, v.x);
				max_v.y = fmaxf(max_v.y, v.y);
			}

			float width_meters = max_v.x - min_v.x;
			float height_meters = max_v.y - min_v.y;

			mrb_value hash = mrb_hash_new(mrb);
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x")),
								  drb_api->mrb_float_value(mrb, center_meters.x * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y")),
								  drb_api->mrb_float_value(mrb, center_meters.y * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "w")),
								  drb_api->mrb_float_value(mrb, width_meters * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "h")),
								  drb_api->mrb_float_value(mrb, height_meters * PIXELS_PER_METER));

			mrb_ary_push(mrb, result_array, hash);
		} // TODO: handle other shapes than b2Polygon (which is currently always a box in our case...)
	}

	mrb_free(mrb, shapeIds);
	return result_array;
}

// angle is also needed to render sprite data for each body. Returns the body's current angle in degrees to conform to DragonRuby idioms
static mrb_value body_angle(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	b2Rot rotation = b2Body_GetRotation(*bodyId);
	float angle_radians = b2Rot_GetAngle(rotation);
	float angle_degrees = angle_radians * (180.0f / M_PI);
	return drb_api->drb_float_value(mrb, angle_degrees);
}

static mrb_value body_set_rotation(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float angle_degrees;
	mrb_get_args(mrb, "f", &angle_degrees);

	float angle_radians = angle_degrees * (M_PI / 180.0f);
	b2Vec2 position = b2Body_GetPosition(*bodyId);
	
	// Manually create the rotation struct from the angle
	b2Rot rotation;
	rotation.s = sinf(angle_radians);
	rotation.c = cosf(angle_radians);

	b2Body_SetTransform(*bodyId, position, rotation);

	return mrb_nil_value();
}

static mrb_value body_apply_force_center(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float force_x, force_y;
	mrb_get_args(mrb, "ff", &force_x, &force_y);

	b2Vec2 force = {force_x / PIXELS_PER_METER, force_y / PIXELS_PER_METER};

	b2Body_ApplyForceToCenter(*bodyId, force, true);

	return mrb_nil_value();
}

static mrb_value body_apply_impulse_center(mrb_state *mrb, mrb_value self) {
	b2BodyId* bodyId = DATA_PTR(self);

	mrb_float force_x, force_y;
	mrb_get_args(mrb, "ff", &force_x, &force_y);
	b2Vec2 impulse = {force_x / PIXELS_PER_METER, force_y / PIXELS_PER_METER };

	b2Body_ApplyLinearImpulseToCenter(*bodyId, impulse, true);
	return mrb_nil_value();
}

static mrb_value body_apply_impulse_for_velocity(mrb_state *mrb, mrb_value self) {
	b2BodyId* bodyId = DATA_PTR(self);

	mrb_float vel_x, vel_y;
	mrb_get_args(mrb, "ff", &vel_x, &vel_y);

	b2Vec2 current_vel = b2Body_GetLinearVelocity(*bodyId);
	float mass = b2Body_GetMass(*bodyId);
	float dvx = vel_x - current_vel.x;
	float dvy = vel_y - current_vel.y;
	b2Vec2 impulse = {0};
	impulse.x = mass * dvx;
	impulse.y = mass * dvy;

	b2Body_ApplyLinearImpulseToCenter(*bodyId, impulse, true);
	return mrb_nil_value();
}

static mrb_value body_rotate_towards_angle(mrb_state *mrb, mrb_value self) {

}

DRB_FFI_EXPORT
void drb_register_c_extensions_with_api(mrb_state *state, struct drb_api_t *api) {
	// Boilerplate and module definitions
	drb_api = api;
	struct RClass *FFI = drb_api->mrb_module_get(state, "FFI");
	struct RClass *module = drb_api->mrb_define_module_under(state, FFI, "Box2D");
	struct RClass *base = state->object_class;
	// World Ruby class definition
	struct RClass *World = drb_api->mrb_define_class_under(state, module, "World", base);
	drb_api->mrb_define_method(state, World, "initialize", world_initialize, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, World, "create_body", world_create_body, MRB_ARGS_ARG(3, 1));
	drb_api->mrb_define_method(state, World, "step", world_step, MRB_ARGS_REQ(1));

	// Body Ruby class definition
	struct RClass *Body = drb_api->mrb_define_class_under(state, module, "Body", base);
	drb_api->mrb_define_method(state, Body, "create_box_shape", body_create_box_shape, MRB_ARGS_ARG(3, 1));
	drb_api->mrb_define_method(state, Body, "create_t_shape", body_create_t_shape, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "create_box_shape_2x2", body_create_box_shape_2x2, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "create_l_shape", body_create_l_shape, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "create_j_shape", body_create_j_shape, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "position", body_position, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "position_meters", body_position_meters, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "extents", body_extents, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "get_shapes_info", body_get_shapes_info, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "angle", body_angle, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "angle=", body_set_rotation, MRB_ARGS_REQ(1));
	drb_api->mrb_define_method(state, Body, "apply_force_center", body_apply_force_center, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "apply_impulse_center", body_apply_impulse_center, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "apply_impulse_for_velocity", body_apply_impulse_for_velocity, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "get_info", body_get_info, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "awake?", body_is_awake, MRB_ARGS_NONE());
}
