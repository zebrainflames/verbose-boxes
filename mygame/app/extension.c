#include "collision.h"
#include "math_functions.h"
#include "mruby.h"
#include "mruby/boxing_word.h"
#include "mruby/value.h"
#include <assert.h>
#include <dragonruby.h>
#include <mruby/array.h>
#include <mruby/data.h>
#include <mruby/proc.h>
#include <mruby/variable.h>
#include <stdbool.h>
#include <string.h>

// testing box2d includes:
#include "box2d.h"
#include "id.h"
#include "types.h"

/*
Why we use drb_api-> wrappers instead of calling mruby C API directly on Windows; on Linux these function calls worked
directly before, but perhaps there is a difference in shared library linking that causes problems. For now, default to
accessing the mruby C API via the DragonRuby API struct.

Applies to things like `mrb_*_get`, `mrb_get_args` etc. - these are used to define our Ruby API & cross ABI boundary.

TODO: study object linking (differences) on different platforms, together with the mruby C API.
*/

#ifndef M_PI
#define M_PI 3.14159265358979323846 // ...should be defined in math.h but we just redefine it here to compile...
#endif

#define RAD2DEG (180.0f / M_PI)
#define DEGTORAD (M_PI / 180.0f)

static drb_api_t *drb_api;
// config
static const float PIXELS_PER_METER = 32.0f; // NOTE: this still needs some tuning. We might want to bring the average energy level down in individual box2d simulation islands

// global game-specific physics state
static b2WorldDef mainWorldDef;
static b2WorldId *main_world_ptr; // this might not be exactly safe...
static Uint32 current_tick = 0;
static Uint32 prev_tick = 0;

// box2d raycasts are used to detect horizontal lines of blocks for the clearing logic
#define MAX_RAY_HITS 50
// raycast_collection_t is a specific collection type for use with the callback functions to get a list of shapes colliding with a raycast
typedef struct {
	b2ShapeId hit_shapes[MAX_RAY_HITS];
	int count;
} raycast_colletion_t;

static float raycast_callback(b2ShapeId shape_id, b2Vec2 point, b2Vec2 normal, float fraction, void *user_data) {
	raycast_colletion_t *collection = (raycast_colletion_t *)user_data;
	if (collection->count < MAX_RAY_HITS) {
		collection->hit_shapes[collection->count] = shape_id;
		collection->count++;
	}
	return 1.0f; // always returning 1.0f makes the raycast always go full length, i.e. not stop on collisions. There might be better ways to do this
}

// Collision filter categories
#define TETROMINO_BIT 0x0001
#define SENSOR_BIT 0x0002
#define GROUND_BIT 0x0004

typedef enum { BODY_TYPE_REGULAR, BODY_TYPE_SENSOR } body_type_t;
// body_user_context provides the tetrimino block -specific gameplay related data, esp. to the ruby side of our codebase -- such as info on
// whether this block collided this frame, that can be used for gameplay logic
typedef struct {
	mrb_value body_obj;
	body_type_t type;
	int contact_count;
	bool collided;
} body_user_context;

// TODO: Do we also need to free all bodies / shapes to avoid leaks on ruby-held objects?
static void b2WorldId_free(mrb_state *mrb, void *p) {
	printf("[CExt] -- INFO: freeing Box2D world");
	b2WorldId *id = (b2WorldId *)p;
	b2DestroyWorld(*id);
	*id = b2_nullWorldId;
	main_world_ptr = NULL;
	drb_api->mrb_free(mrb, p);
}

static const struct mrb_data_type b2WorldId_type = {
	"b2WorldId",
	b2WorldId_free,
};

static void b2BodyId_free(mrb_state *mrb, void *p) {
	if (!p) {
		printf("WARNING: Tried to free a null b2BodyId & buc!\n");
		return;
	}
	b2BodyId *bodyId = (b2BodyId *)p;
	if (!b2Body_IsValid(*bodyId)) {
		printf("WARNING: Tried to free an invalid body!");
		return; // TODO: this might lead to dangling bucs, but let's see how this behaves first
	}
	body_user_context *buc = (body_user_context *)b2Body_GetUserData(*bodyId);
	if (buc) {
		drb_api->mrb_free(mrb, buc);
	}
	b2DestroyBody(*(b2BodyId *)p);
	drb_api->mrb_free(mrb, p);
}

static const struct mrb_data_type b2BodyId_type = {
	"b2BodyId",
	b2BodyId_free,
};

static b2Vec2 pixels_to_meters(float x, float y) { return (b2Vec2){x / PIXELS_PER_METER, y / PIXELS_PER_METER}; }

static b2Vec2 meters_to_pixels(float x, float y) { return (b2Vec2){x * PIXELS_PER_METER, y * PIXELS_PER_METER}; }

static mrb_value world_initialize(mrb_state *mrb, mrb_value self) {
	mainWorldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&mainWorldDef);
	b2World_SetGravity(worldId, (b2Vec2){0.0f, -9.8f});

	b2WorldId *worldId_ptr = (b2WorldId *)drb_api->mrb_malloc(mrb, sizeof(b2WorldId));
	*worldId_ptr = worldId;

	mrb_data_init(self, worldId_ptr, &b2WorldId_type);

	return self;
}

static mrb_value world_create_body(mrb_state *mrb, mrb_value self) {
	b2WorldId *worldId = DATA_PTR(self);
	// printf("[CExt] -- INFO: Creating Body...\n");
	mrb_value type_str;
	mrb_float x, y;
	mrb_bool allow_sleep = true;
	mrb_float vx = 0.0, vy = 0.0, av = 0.0;
	drb_api->mrb_get_args(mrb, "Sff|bfff", &type_str, &x, &y, &allow_sleep, &vx, &vy, &av);

	struct RClass *module = drb_api->mrb_module_get(mrb, "FFI");
	module = drb_api->mrb_module_get_under(mrb, module, "Box2D");
	struct RClass *body_class = drb_api->mrb_class_get_under(mrb, module, "Body");
	mrb_value body_obj = drb_api->mrb_obj_new(mrb, body_class, 0, NULL);

	drb_api->mrb_iv_set(mrb, body_obj, drb_api->mrb_intern_lit(mrb, "@contacts"), drb_api->mrb_ary_new(mrb));

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.position = pixels_to_meters(x, y);
	b2Vec2 linear_vel_meters = pixels_to_meters(vx, vy);
	bodyDef.linearVelocity = linear_vel_meters;
	bodyDef.angularVelocity = av * DEGTORAD;

	body_user_context *holder = (body_user_context *)drb_api->mrb_malloc(mrb, sizeof(body_user_context));
	holder->body_obj = body_obj;
	holder->type = BODY_TYPE_REGULAR;
	holder->contact_count = 0;
	holder->collided = false;
	bodyDef.userData = holder;

	b2BodyType type = b2_staticBody;
	if (strcmp(drb_api->mrb_str_to_cstr(mrb, type_str), "dynamic") == 0) {
		type = b2_dynamicBody;
		bodyDef.linearDamping = 0.2f;
		bodyDef.angularDamping = 0.6f;
		bodyDef.enableSleep = allow_sleep;
	} else if (strcmp(drb_api->mrb_str_to_cstr(mrb, type_str), "kinematic") == 0) {
		type = b2_kinematicBody;
	}

	bodyDef.type = type;
	b2BodyId bodyId = b2CreateBody(*worldId, &bodyDef);

	b2BodyId *bodyId_ptr = (b2BodyId *)drb_api->mrb_malloc(mrb, sizeof(b2BodyId));
	*bodyId_ptr = bodyId;

	mrb_data_init(body_obj, bodyId_ptr, &b2BodyId_type);

	return body_obj;
}

static mrb_value body_create_sensor_box(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float width, height;
	drb_api->mrb_get_args(mrb, "ff", &width, &height);

	assert(width > 0.0f && "width cannot be zero or negative");
	assert(height > 0.0f && "height cannot be zero or negative");

	b2Vec2 half_extents_meters = (b2Vec2){width / PIXELS_PER_METER / 2.0f, height / PIXELS_PER_METER / 2.0f};
	b2Polygon box = b2MakeBox(half_extents_meters.x, half_extents_meters.y);
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.isSensor = true;
	shapeDef.enableSensorEvents = true; // Enable sensor events for the sensor
	shapeDef.filter.categoryBits = SENSOR_BIT;
	shapeDef.filter.maskBits = TETROMINO_BIT;

	body_user_context *holder = (body_user_context *)b2Body_GetUserData(*bodyId);
	if (holder) {
		holder->type = BODY_TYPE_SENSOR;
		holder->contact_count = 0;
	}

	b2CreatePolygonShape(*bodyId, &shapeDef, &box);

	return mrb_nil_value();
}

static mrb_value body_create_box_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float width, height, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	mrb_bool enable_contacts = false;
	drb_api->mrb_get_args(mrb, "fff|ffb", &width, &height, &density, &friction, &restitution, &enable_contacts);

	assert(width > 0.0f && "width cannot be zero or negative");
	assert(height > 0.0f && "height cannot be zero or negative");

	b2Vec2 half_extents_meters = (b2Vec2){width / PIXELS_PER_METER / 2.0f, height / PIXELS_PER_METER / 2.0f};
	b2Polygon box = b2MakeBox(half_extents_meters.x, half_extents_meters.y);
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = enable_contacts;
	b2CreatePolygonShape(*bodyId, &shapeDef, &box);

	return mrb_nil_value();
}

// Helper function to create an offset polygon for a box using b2MakeBox and translation
// This is used to easily create the tetriminos
static void create_offset_box_fixture(b2BodyId bodyId, const b2ShapeDef *shapeDef, float box_width_px, float box_height_px, float offset_x_px, float offset_y_px) {
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
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, square_size_px);

	return mrb_nil_value();
}

static mrb_value body_create_box_shape_2x2(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef;
	shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

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
//   ##
static mrb_value body_create_l_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	// Horizontal bar
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);

	// Vertical bar
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, square_size_px);
	// create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 2 * square_size_px);

	return mrb_nil_value();
}

// J-shape (mirrored L). Origin is the center of the 3-block segment.
//   #
//   #
//  ##
static mrb_value body_create_j_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, square_size_px);

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);

	return mrb_nil_value();
}

// S-shape. Origin is at the center of the shape
//    #
//  ##
//  #
static mrb_value body_create_s_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, square_size_px);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, square_size_px);

	return mrb_nil_value();
}

// Z-shape (mirrored S). Origin is at the center of the shape
//  #
//  ##
//   #
static mrb_value body_create_z_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, square_size_px, 0);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, square_size_px);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, -square_size_px, square_size_px);

	return mrb_nil_value();
}

// I-shape. Origin is the center of the 4-block segment.
//   #
//   #
//   #
//   #
static mrb_value body_create_i_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float square_size_px, density;
	mrb_float friction = 0.5f;
	mrb_float restitution = 0.1f;
	drb_api->mrb_get_args(mrb, "ff|ff", &square_size_px, &density, &friction, &restitution);

	b2ShapeDef shapeDef = b2DefaultShapeDef();

	shapeDef.density = density;
	shapeDef.material.friction = friction;
	shapeDef.material.restitution = restitution;
	shapeDef.enableContactEvents = true;
	shapeDef.enableSensorEvents = true;
	shapeDef.filter.categoryBits = TETROMINO_BIT;
	shapeDef.filter.maskBits = GROUND_BIT | SENSOR_BIT | TETROMINO_BIT;

	float half = square_size_px / 2.0f;
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, -half - square_size_px);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, half + square_size_px);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, half);
	create_offset_box_fixture(*bodyId, &shapeDef, square_size_px, square_size_px, 0, -half);

	return mrb_nil_value();
}

static mrb_value body_create_chain_shape(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_value points_array;
	mrb_bool loop;
	mrb_float friction = 1.0f;
	mrb_float restitution = 0.0f;

	drb_api->mrb_get_args(mrb, "A!b|ff", &points_array, &loop, &friction, &restitution);

	int num_points = RARRAY_LEN(points_array);
	if (num_points < 2) {
		// A chain needs at least 2 points
		return mrb_nil_value();
	}

	b2Vec2 *points = drb_api->mrb_malloc(mrb, sizeof(b2Vec2) * num_points);
	for (int i = 0; i < num_points; i++) {
		mrb_value point_hash = drb_api->mrb_ary_entry(points_array, i);
		mrb_value x_val = drb_api->mrb_hash_get(mrb, point_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "x")));
		mrb_value y_val = drb_api->mrb_hash_get(mrb, point_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "y")));

		float x = drb_api->mrb_to_flo(mrb, x_val);
		float y = drb_api->mrb_to_flo(mrb, y_val);

		points[i] = pixels_to_meters(x, y);
	}

	b2ChainDef chainDef = b2DefaultChainDef();

	chainDef.points = points;
	chainDef.count = num_points;
	chainDef.isLoop = loop;
	chainDef.filter.categoryBits = GROUND_BIT;
	chainDef.filter.maskBits = TETROMINO_BIT;

    // Surface material from Ruby side (optional friction and restitution)
    b2SurfaceMaterial surface_mat = (b2SurfaceMaterial){0};
    surface_mat.friction = friction;
    surface_mat.restitution = restitution;
    chainDef.materials = &surface_mat;
    chainDef.materialCount = 1;


	b2CreateChain(*bodyId, &chainDef);

	drb_api->mrb_free(mrb, points);

	return mrb_nil_value();
}

#define MAX_DELTA 0.032f

static float get_delta_time() {
	Uint32 ticks = drb_api->SDL_GetTicks();
	current_tick = ticks;
	float dt = (float)(current_tick - prev_tick) / 1000.0f;
	// tad hacky, but sometimes the simulation gets called with a very long pause in ticks (in Box2D side) - to avoid
	// and unstable simulation (== wildly flying pieces) we constrain the delta time to some sane limit that needs tuning
	if (dt > MAX_DELTA)
		dt = MAX_DELTA;
	prev_tick = current_tick;
	return dt;
}

typedef struct {
	b2ShapeId shape_id;
	b2Vec2 pos;
} ShapeInfo;

int compare_shapes(const void *a, const void *b) {
	ShapeInfo *shapeA = (ShapeInfo *)a;
	ShapeInfo *shapeB = (ShapeInfo *)b;
	return (shapeA->pos.x > shapeB->pos.x) - (shapeA->pos.x < shapeB->pos.x);
}

typedef struct {
	b2ShapeId shape_id;
	b2Vec2 world_pos_pixels;
} line_hit;

// Comparison function for qsort to sort hits by their X-coordinate
static int compare_hits_by_x(const void *a, const void *b) {
	line_hit *hit_a = (line_hit *)a;
	line_hit *hit_b = (line_hit *)b;
	if (hit_a->world_pos_pixels.x < hit_b->world_pos_pixels.x)
		return -1;
	if (hit_a->world_pos_pixels.x > hit_b->world_pos_pixels.x)
		return 1;
	return 0;
}

// main line clear checking function - finds the shapes forming a suitable line at least `min_hits` long, return shape origins in world
// pixel space (for effects) and list of affected bodies NOTE: unlike the rest of the C code, this is very much about game logic; could
// perhaps rather be done in Ruby
static mrb_value world_raycast(mrb_state *mrb, mrb_value self) {
	b2WorldId *worldId = DATA_PTR(self);
	mrb_float x1, y1, x2, y2;
	mrb_int min_hits = 6;
	mrb_float vertical_tolerance = 6.0f;
	mrb_float horizontal_tolerance = 32.0f * 1.2f;

	drb_api->mrb_get_args(mrb, "ffff|iff", &x1, &y1, &x2, &y2, &min_hits, &vertical_tolerance, &horizontal_tolerance);

	b2Vec2 p1 = pixels_to_meters(x1, y1);
	b2Vec2 p2 = pixels_to_meters(x2, y2);
	b2Vec2 tr = b2Sub(p2, p1);

	raycast_colletion_t ray_collection = {0};
	b2QueryFilter filter = b2DefaultQueryFilter();
	filter.maskBits = TETROMINO_BIT;

	b2World_CastRay(*worldId, p1, tr, filter, raycast_callback, &ray_collection);

	mrb_value results = drb_api->mrb_hash_new(mrb);
	drb_api->mrb_hash_set(mrb, results, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "bodies_to_split")),
						  drb_api->mrb_ary_new(mrb));
	drb_api->mrb_hash_set(mrb, results, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "cleared_points")),
						  drb_api->mrb_ary_new(mrb));

	// DEBUG: All raycast hits are returned for debug purposes (display on Ruby side)
	mrb_value all_hits_ary = drb_api->mrb_ary_new(mrb);
	drb_api->mrb_hash_set(mrb, results, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "all_hits")), all_hits_ary);

	if (ray_collection.count < min_hits) {
		return results;
	}

	line_hit *candidates = drb_api->mrb_malloc(mrb, sizeof(line_hit) * ray_collection.count);
	int candidate_count = 0;
	float total_y = 0;
	const float max_velocity_sq = 0.01f * 0.01f;

	// we filter hits by velocity and alignment to try to only match relatively stable horizontal lines
	for (int i = 0; i < ray_collection.count; ++i) {
		b2ShapeId shape_id = ray_collection.hit_shapes[i];
		if (!b2Shape_IsValid(shape_id))
			continue;

		b2BodyId body_id = b2Shape_GetBody(shape_id);
		if (b2LengthSquared(b2Body_GetLinearVelocity(body_id)) > max_velocity_sq) {
			continue;
		}

		b2Transform transform = b2Body_GetTransform(body_id);
		b2Polygon poly = b2Shape_GetPolygon(shape_id);
		b2Vec2 world_pos_meters = b2TransformPoint(transform, poly.centroid);

		b2Vec2 pixel_pos = meters_to_pixels(world_pos_meters.x, world_pos_meters.y);

		// DEBUG: populating the all_hits array in `results`
		mrb_value hit_hash = drb_api->mrb_hash_new(mrb);
		drb_api->mrb_hash_set(mrb, hit_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "x")),
							  drb_api->mrb_float_value(mrb, pixel_pos.x));
		drb_api->mrb_hash_set(mrb, hit_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "y")),
							  drb_api->mrb_float_value(mrb, pixel_pos.y));
		drb_api->mrb_ary_push(mrb, all_hits_ary, hit_hash);

		candidates[candidate_count].shape_id = shape_id;
		candidates[candidate_count].world_pos_pixels = pixel_pos;
		total_y += candidates[candidate_count].world_pos_pixels.y;
		candidate_count++;
	}

	if (candidate_count < min_hits) {
		drb_api->mrb_free(mrb, candidates);
		return results;
	}

	float avg_y = total_y / candidate_count;
	line_hit *aligned_hits = drb_api->mrb_malloc(mrb, sizeof(line_hit) * candidate_count);
	int aligned_count = 0;
	for (int i = 0; i < candidate_count; ++i) {
		if (fabs(candidates[i].world_pos_pixels.y - avg_y) < vertical_tolerance) {
			aligned_hits[aligned_count++] = candidates[i];
		}
	}
	drb_api->mrb_free(mrb, candidates);

	if (aligned_count < min_hits) {
		drb_api->mrb_free(mrb, aligned_hits);
		return results;
	}

	// TODO: Does Box2D give any guarantees on hit order - meaning is this necessary?
	qsort(aligned_hits, aligned_count, sizeof(line_hit), compare_hits_by_x);

	line_hit *largest_group = NULL;
	int max_group_size = 0;
	int current_group_start = 0;
	// find largest group, i.e. biggest grouping of vertically aligned shapes within horizontal limits
	for (int i = 1; i <= aligned_count; ++i) {
		// A group ends if we reach the end of the array OR the next shape is too far away
		if (i == aligned_count || aligned_hits[i].world_pos_pixels.x - aligned_hits[i - 1].world_pos_pixels.x > horizontal_tolerance) {
			int current_group_size = i - current_group_start;
			if (current_group_size > max_group_size) {
				max_group_size = current_group_size;
				largest_group = &aligned_hits[current_group_start];
			}
			current_group_start = i;
		}
	}

	// we can now assume the 'largest group' constitues an approximation of a horizontal line -> destroy the shapes it has and return the
	// list of affected bodies from it
	if (max_group_size >= min_hits) {
		mrb_value bodies_to_split_ary =
			drb_api->mrb_hash_get(mrb, results, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "bodies_to_split")));
		mrb_value cleared_points_ary =
			drb_api->mrb_hash_get(mrb, results, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "cleared_points")));

		b2BodyId *unique_bodies = drb_api->mrb_malloc(mrb, sizeof(b2BodyId) * max_group_size);
		int unique_body_count = 0;

		for (int i = 0; i < max_group_size; ++i) {
			line_hit hit = largest_group[i];
			b2BodyId body_id = b2Shape_GetBody(hit.shape_id);

			bool found = false;
			for (int j = 0; j < unique_body_count; j++) {
				if (unique_bodies[j].index1 == body_id.index1) {
					found = true;
					break;
				}
			}
			if (!found) {
				unique_bodies[unique_body_count++] = body_id;
			}

			// Add the shape's position to the return data for visual effects
			mrb_value hit_hash = drb_api->mrb_hash_new(mrb);
			drb_api->mrb_hash_set(mrb, hit_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "x")),
								  drb_api->mrb_float_value(mrb, hit.world_pos_pixels.x));
			drb_api->mrb_hash_set(mrb, hit_hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "y")),
								  drb_api->mrb_float_value(mrb, hit.world_pos_pixels.y));
			drb_api->mrb_ary_push(mrb, cleared_points_ary, hit_hash);

			b2DestroyShape(hit.shape_id, true);
		}

		// Second, populate the Ruby array with the affected body objects
		for (int i = 0; i < unique_body_count; i++) {
			b2BodyId body_id = unique_bodies[i];
			if (b2Body_IsValid(body_id)) {
				body_user_context *buc = (body_user_context *)b2Body_GetUserData(body_id);
				if (buc && !mrb_nil_p(buc->body_obj)) {
					drb_api->mrb_ary_push(mrb, bodies_to_split_ary, buc->body_obj);
				}
			}
		}
		drb_api->mrb_free(mrb, unique_bodies);
	}

	drb_api->mrb_free(mrb, aligned_hits);
	return results;
}

static mrb_value world_step(mrb_state *mrb, mrb_value self) {
	b2WorldId *worldId = DATA_PTR(self);

	float dt = get_delta_time();
	b2World_Step(*worldId, dt, 8);

	b2SensorEvents sensorEvents = b2World_GetSensorEvents(*worldId);
	for (int i = 0; i < sensorEvents.beginCount; ++i) {
		b2SensorBeginTouchEvent event = sensorEvents.beginEvents[i];
		b2BodyId sensorBodyId = b2Shape_GetBody(event.sensorShapeId);
		body_user_context *sensorHolder = (body_user_context *)b2Body_GetUserData(sensorBodyId);
		if (sensorHolder && sensorHolder->type == BODY_TYPE_SENSOR) {
			sensorHolder->contact_count++;
		}
	}

	for (int i = 0; i < sensorEvents.endCount; ++i) {
		b2SensorEndTouchEvent event = sensorEvents.endEvents[i];
		b2BodyId sensorBodyId = b2Shape_GetBody(event.sensorShapeId);
		body_user_context *sensorHolder = (body_user_context *)b2Body_GetUserData(sensorBodyId);
		if (sensorHolder && sensorHolder->type == BODY_TYPE_SENSOR) {
			sensorHolder->contact_count--;
		}
	}

	b2ContactEvents events = b2World_GetContactEvents(*worldId);
	for (int i = 0; i < events.beginCount; ++i) {
		b2ContactBeginTouchEvent event = events.beginEvents[i];
		b2BodyId bodyIdA = b2Shape_GetBody(event.shapeIdA);
		b2BodyId bodyIdB = b2Shape_GetBody(event.shapeIdB);

		body_user_context *holderA = (body_user_context *)b2Body_GetUserData(bodyIdA);
		body_user_context *holderB = (body_user_context *)b2Body_GetUserData(bodyIdB);

		if (holderA)
			holderA->collided = true;
		if (holderB)
			holderB->collided = true;
	}

	return mrb_nil_value();
}

static mrb_value body_destroy(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId_ptr = DATA_PTR(self);
	if (bodyId_ptr && b2Body_IsValid(*bodyId_ptr)) {
		b2BodyId bodyId = *bodyId_ptr;
		body_user_context *buc = (body_user_context *)b2Body_GetUserData(bodyId);
		if (buc) {
			drb_api->mrb_free(mrb, buc);
		}
		b2DestroyBody(bodyId);
	}

	if (bodyId_ptr) {
		drb_api->mrb_free(mrb, bodyId_ptr);
	}
	DATA_PTR(self) = NULL;

	return mrb_nil_value();
}

static mrb_value body_has_collided(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	body_user_context *buc = (body_user_context *)b2Body_GetUserData(*bodyId);
	if (buc) {
		return mrb_bool_value(buc->collided);
	}
	return mrb_false_value();
}

static mrb_value body_get_info(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	b2Vec2 pos = b2Body_GetPosition(*bodyId);
	b2Vec2 vel = b2Body_GetLinearVelocity(*bodyId);
	float ang_vel = b2Body_GetAngularVelocity(*bodyId);
	b2Rot rot = b2Body_GetRotation(*bodyId);
	float angle = b2Rot_GetAngle(rot);
	bool awake = b2Body_IsAwake(*bodyId);

	mrb_value hash = drb_api->mrb_hash_new(mrb);

	b2Vec2 pos_pixels = meters_to_pixels(pos.x, pos.y);
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "x")),
						  drb_api->mrb_float_value(mrb, pos_pixels.x));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "y")),
						  drb_api->mrb_float_value(mrb, pos_pixels.y));

	b2Vec2 vel_pixels = meters_to_pixels(vel.x, vel.y);
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "vx")),
						  drb_api->mrb_float_value(mrb, vel_pixels.x));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "vy")),
						  drb_api->mrb_float_value(mrb, vel_pixels.y));

	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "angle")),
						  drb_api->mrb_float_value(mrb, angle * RAD2DEG));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "angular_velocity")),
						  drb_api->mrb_float_value(mrb, ang_vel * RAD2DEG));
	drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_lit(mrb, "awake")), mrb_bool_value(awake));

	return hash;
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
		float width_meters = polygon.vertices[1].x - polygon.vertices[0].x;	 // (halfWidth - (-halfWidth)) = 2 * halfWidth
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
		return drb_api->mrb_ary_new(mrb); // NOTE: should probably just return nil here!
	}

	// Allocate memory to hold the shape IDs
	b2ShapeId *shapeIds = drb_api->mrb_malloc(mrb, sizeof(b2ShapeId) * shapeCount);
	b2Body_GetShapes(*bodyId, shapeIds, shapeCount);

	mrb_value result_array = drb_api->mrb_ary_new_capa(mrb, shapeCount);

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

			mrb_value hash = drb_api->mrb_hash_new(mrb);
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x")),
								  drb_api->mrb_float_value(mrb, center_meters.x * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y")),
								  drb_api->mrb_float_value(mrb, center_meters.y * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "w")),
								  drb_api->mrb_float_value(mrb, width_meters * PIXELS_PER_METER));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "h")),
								  drb_api->mrb_float_value(mrb, height_meters * PIXELS_PER_METER));

			drb_api->mrb_ary_push(mrb, result_array, hash);
		} else if (b2Shape_GetType(shapeId) == b2_chainSegmentShape) {
			b2ChainSegment segment = b2Shape_GetChainSegment(shapeId);
			b2Vec2 p1 = meters_to_pixels(segment.segment.point1.x, segment.segment.point1.y);
			b2Vec2 p2 = meters_to_pixels(segment.segment.point2.x, segment.segment.point2.y);

			mrb_value hash = drb_api->mrb_hash_new(mrb);
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x1")),
								  drb_api->mrb_float_value(mrb, p1.x));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y1")),
								  drb_api->mrb_float_value(mrb, p1.y));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "x2")),
								  drb_api->mrb_float_value(mrb, p2.x));
			drb_api->mrb_hash_set(mrb, hash, drb_api->mrb_symbol_value(drb_api->mrb_intern_cstr(mrb, "y2")),
								  drb_api->mrb_float_value(mrb, p2.y));

			drb_api->mrb_ary_push(mrb, result_array, hash);
		}
	}

	drb_api->mrb_free(mrb, shapeIds);
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
	drb_api->mrb_get_args(mrb, "f", &angle_degrees);

	float angle_radians = angle_degrees * (M_PI / 180.0f);
	b2Vec2 position = b2Body_GetPosition(*bodyId);

	// Manually create the rotation struct from the angle
	b2Rot rotation;
	rotation.s = sinf(angle_radians);
	rotation.c = cosf(angle_radians);

	b2Body_SetTransform(*bodyId, position, rotation);

	return mrb_nil_value();
}

static mrb_value body_set_angular_velocity(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	mrb_float velocity_deg_per_sec;
	drb_api->mrb_get_args(mrb, "f", &velocity_deg_per_sec);
	b2Body_SetAngularVelocity(*bodyId, velocity_deg_per_sec * DEGTORAD);
	return mrb_nil_value();
}

static mrb_value body_apply_force_center(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float force_x, force_y;
	drb_api->mrb_get_args(mrb, "ff", &force_x, &force_y);

	b2Vec2 force = {force_x / PIXELS_PER_METER, force_y / PIXELS_PER_METER};

	b2Body_ApplyForceToCenter(*bodyId, force, true);

	return mrb_nil_value();
}

static mrb_value body_apply_impulse_center(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float force_x, force_y;
	drb_api->mrb_get_args(mrb, "ff", &force_x, &force_y);
	b2Vec2 impulse = {force_x / PIXELS_PER_METER, force_y / PIXELS_PER_METER};

	b2Body_ApplyLinearImpulseToCenter(*bodyId, impulse, true);
	return mrb_nil_value();
}

static mrb_value body_apply_impulse_for_velocity(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);

	mrb_float vel_x, vel_y;
	drb_api->mrb_get_args(mrb, "ff", &vel_x, &vel_y);

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

#define MAX_ROT_DEGREES 5 // TODO: this should be just up to the parameters to body_rotate -> move to Ruby

// TODO: clean up this mess...
static mrb_value body_rotate(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	b2Rot rotation = b2Body_GetRotation(*bodyId);
	float angle_radians = b2Rot_GetAngle(rotation);

	mrb_float delta_angle_degrees;
	drb_api->mrb_get_args(mrb, "f", &delta_angle_degrees);
	float delta_radians = delta_angle_degrees * (M_PI / 180.0f);
	float next_angle = angle_radians + b2Body_GetAngularVelocity(*bodyId) / 60.0f;
	float total_rotation = angle_radians + delta_radians - next_angle;

	while (total_rotation < -180.0f * DEGTORAD)
		total_rotation += 360.0f * DEGTORAD;
	while (total_rotation > 180.0f * DEGTORAD)
		total_rotation -= 360.0f * DEGTORAD;
	float desiredAngularVelocity = total_rotation * 60;
	float change = MAX_ROT_DEGREES * DEGTORAD;
	desiredAngularVelocity = fminf(change, fmaxf(-change, desiredAngularVelocity));
	float impulse = b2Body_GetRotationalInertia(*bodyId) * desiredAngularVelocity;
	b2Body_ApplyAngularImpulse(*bodyId, impulse, true);

	return mrb_nil_value();
}

static mrb_value body_get_contacts(mrb_state *mrb, mrb_value self) {
	return drb_api->mrb_iv_get(mrb, self, drb_api->mrb_intern_lit(mrb, "@contacts"));
}

static mrb_value body_get_sensor_contact_count(mrb_state *mrb, mrb_value self) {
	b2BodyId *bodyId = DATA_PTR(self);
	body_user_context *holder = (body_user_context *)b2Body_GetUserData(*bodyId);
	if (holder && holder->type == BODY_TYPE_SENSOR) {
		return drb_api->mrb_int_value(mrb, holder->contact_count);
	}
	return drb_api->mrb_int_value(mrb, 0);
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
	drb_api->mrb_define_method(state, World, "initialize", world_initialize, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, World, "create_body", world_create_body, MRB_ARGS_ARG(3, 4));
	drb_api->mrb_define_method(state, World, "step", world_step, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, World, "raycast", world_raycast, MRB_ARGS_ARG(4, 3));

	// Body Ruby class definition
	struct RClass *Body = drb_api->mrb_define_class_under(state, module, "Body", base);
	drb_api->mrb_define_method(state, Body, "create_box_shape", body_create_box_shape, MRB_ARGS_ARG(3, 3));
	drb_api->mrb_define_method(state, Body, "create_sensor_box", body_create_sensor_box, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "create_t_shape", body_create_t_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_box_shape_2x2", body_create_box_shape_2x2, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_l_shape", body_create_l_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_j_shape", body_create_j_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_i_shape", body_create_i_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_s_shape", body_create_s_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "create_z_shape", body_create_z_shape, MRB_ARGS_ARG(2, 2));
	//TODO: create S and Z shapes...
	drb_api->mrb_define_method(state, Body, "create_chain_shape", body_create_chain_shape, MRB_ARGS_ARG(2, 2));
	drb_api->mrb_define_method(state, Body, "position", body_position, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "position_meters", body_position_meters, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "extents", body_extents, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "get_shapes_info", body_get_shapes_info, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "angle", body_angle, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "angle=", body_set_rotation, MRB_ARGS_REQ(1));
	drb_api->mrb_define_method(state, Body, "angular_velocity=", body_set_angular_velocity, MRB_ARGS_REQ(1));
	drb_api->mrb_define_method(state, Body, "rotate", body_rotate, MRB_ARGS_REQ(1));
	drb_api->mrb_define_method(state, Body, "apply_force_center", body_apply_force_center, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "apply_impulse_center", body_apply_impulse_center, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "apply_impulse_for_velocity", body_apply_impulse_for_velocity, MRB_ARGS_REQ(2));
	drb_api->mrb_define_method(state, Body, "get_info", body_get_info, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "awake?", body_is_awake, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "collided?", body_has_collided, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "destroy", body_destroy, MRB_ARGS_NONE());
	// TODO: remove old contact mehods which aren't used anymore?
	drb_api->mrb_define_method(state, Body, "contacts", body_get_contacts, MRB_ARGS_NONE());
	drb_api->mrb_define_method(state, Body, "sensor_contact_count", body_get_sensor_contact_count, MRB_ARGS_NONE());
}
