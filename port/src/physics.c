// physics: see physics.h.
#include "physics.h"

#include <math.h>
#include <stdint.h>
#include "raylib.h"
#include "values.h"

// No PhysicsMaterial2D exists in the project and every m_Material is null,
// so Unity's default material applies: friction 0.4, bounciness 0
// (INVENTORY.md section 5.1). Box2D's own default friction is 0.6.
#define UNITY_DEFAULT_FRICTION 0.4f
#define UNITY_DEFAULT_BOUNCINESS 0.0f

// ponytail: fixed tables with linear searches. The game has about 120
// bodies and a few hundred touching pairs; grow these if that changes.
#define MAX_COLLIDERS 1024
#define MAX_SHAPES 4096
#define MAX_TOUCHES 4096
#define MAX_PIECES 8

typedef struct {
    Collider info;
    b2BodyId body;
    b2ShapeId shapes[MAX_PIECES];
    int shapeCount;
    bool used;
} Slot;

// Shapes by Box2D index. A destroyed shape's entry is cleared, and a new
// shape at the same index has a new generation, so a late event for an
// old shape finds no owner.
typedef struct {
    uint16_t generation;
    int collider; // -1 when none
} ShapeOwner;

// One pair of Box2D shapes touching. a and b are shape keys.
typedef struct {
    uint32_t a, b;
    int colA, colB;
    bool trigger;
} Touch;

static b2WorldId world;
static PhysicsHandler handler;
static Slot colliders[MAX_COLLIDERS];
static ShapeOwner owners[MAX_SHAPES];
static Touch touches[MAX_TOUCHES];
static int touchCount;

static uint32_t shape_key(b2ShapeId s)
{
    return (uint32_t)s.index1 << 16 | s.generation;
}

static int shape_owner(b2ShapeId s)
{
    int i = s.index1 - 1;
    if (i < 0 || i >= MAX_SHAPES || owners[i].generation != s.generation) return -1;
    return owners[i].collider;
}

float physics_damping(float unityDrag)
{
    float h = FIXED_TIMESTEP;
    return PHYSICS_SUB_STEPS / h * (powf(1 + h * unityDrag, 1.0f / PHYSICS_SUB_STEPS) - 1);
}

void physics_init(PhysicsHandler h)
{
    // Box2D's size tolerances scale with this. It must come before any
    // other Box2D call (plan section 6.3, item 4).
    b2SetLengthUnitsPerMeter(LENGTH_UNITS_PER_METER);
    b2WorldDef def = b2DefaultWorldDef();
    // Unity's gravity, though every body has gravity scale 0.
    def.gravity = (b2Vec2){PHYSICS_GRAVITY_X, PHYSICS_GRAVITY_Y};
    world = b2CreateWorld(&def);
    handler = h;
    for (int i = 0; i < MAX_COLLIDERS; i++) colliders[i].used = false;
    for (int i = 0; i < MAX_SHAPES; i++) owners[i] = (ShapeOwner){0, -1};
    touchCount = 0;
}

void physics_shutdown(void)
{
    if (b2World_IsValid(world)) b2DestroyWorld(world);
    world = b2_nullWorldId;
}

b2WorldId physics_world(void)
{
    return world;
}

b2BodyId physics_body(b2BodyType type, b2Vec2 position, float linearDrag, float angularDrag, float gravityScale,
                      bool fixedRotation)
{
    b2BodyDef def = b2DefaultBodyDef();
    def.type = type;
    def.position = position;
    def.linearDamping = physics_damping(linearDrag);
    def.angularDamping = physics_damping(angularDrag);
    def.gravityScale = gravityScale;
    def.fixedRotation = fixedRotation;
    def.sleepThreshold = PHYSICS_LINEAR_SLEEP_TOLERANCE;
    return b2CreateBody(world, &def);
}

static b2ShapeDef shape_def(const Collider *c)
{
    b2ShapeDef def = b2DefaultShapeDef();
    def.material.friction = UNITY_DEFAULT_FRICTION;
    def.material.restitution = UNITY_DEFAULT_BOUNCINESS;
    def.isSensor = c->isTrigger;
    // Both are off by default in Box2D 3. Sensors need sensor events on
    // the visiting shape too, so every shape turns them on.
    def.enableSensorEvents = true;
    def.enableContactEvents = !c->isTrigger;
    def.updateBodyMass = false; // physics_set_mass sets it once, at the end
    return def;
}

static int collider_new(b2BodyId body, Collider c)
{
    for (int i = 0; i < MAX_COLLIDERS; i++) {
        if (colliders[i].used) continue;
        colliders[i] = (Slot){.info = c, .body = body, .used = true};
        return i;
    }
    TraceLog(LOG_FATAL, "physics: more than %d colliders", MAX_COLLIDERS);
    return -1;
}

static void collider_add_shape(int c, b2ShapeId s)
{
    Slot *slot = &colliders[c];
    int i = s.index1 - 1;
    if (slot->shapeCount == MAX_PIECES || i >= MAX_SHAPES) {
        TraceLog(LOG_FATAL, "physics: more than %d pieces or %d shapes", MAX_PIECES, MAX_SHAPES); // exits
        return;
    }
    slot->shapes[slot->shapeCount++] = s;
    owners[i] = (ShapeOwner){s.generation, c};
}

int physics_circle(b2BodyId body, Collider c, b2Vec2 center, float radius)
{
    int id = collider_new(body, c);
    b2ShapeDef def = shape_def(&c);
    b2Circle circle = {center, radius};
    collider_add_shape(id, b2CreateCircleShape(body, &def, &circle));
    return id;
}

int physics_box(b2BodyId body, Collider c, b2Vec2 center, b2Vec2 size)
{
    int id = collider_new(body, c);
    b2ShapeDef def = shape_def(&c);
    b2Polygon box = b2MakeOffsetBox(size.x / 2, size.y / 2, center, b2Rot_identity);
    collider_add_shape(id, b2CreatePolygonShape(body, &def, &box));
    return id;
}

// One convex piece, mirrored in x or not. tools/extract.py made every
// piece pass the hull check, and tests/check.c checks it again.
static b2Polygon piece_polygon(const float *xy, int count, bool mirrored)
{
    b2Vec2 p[B2_MAX_POLYGON_VERTICES];
    for (int k = 0; k < count && k < B2_MAX_POLYGON_VERTICES; k++) {
        p[k] = (b2Vec2){mirrored ? -xy[2 * k] : xy[2 * k], xy[2 * k + 1]};
    }
    b2Hull hull = b2ComputeHull(p, count);
    return b2MakePolygon(&hull, 0);
}

int physics_polygon(b2BodyId body, Collider c, const float *points, const int *sizes, int pieces)
{
    int id = collider_new(body, c);
    b2ShapeDef def = shape_def(&c);
    for (int i = 0; i < pieces; i++) {
        b2Polygon poly = piece_polygon(points, sizes[i], false);
        collider_add_shape(id, b2CreatePolygonShape(body, &def, &poly));
        points += 2 * sizes[i];
    }
    return id;
}

void physics_polygon_mirror(int collider, const float *points, const int *sizes, int pieces, bool mirrored)
{
    const Slot *slot = &colliders[collider];
    for (int i = 0; i < pieces && i < slot->shapeCount; i++) {
        b2Polygon poly = piece_polygon(points, sizes[i], mirrored);
        b2Shape_SetPolygon(slot->shapes[i], &poly);
        points += 2 * sizes[i];
    }
}

void physics_set_mass(b2BodyId body, float mass)
{
    b2Body_ApplyMassFromShapes(body);
    b2MassData m = b2Body_GetMassData(body);
    // Unity 5.4 sets the mass directly. Scaling Box2D's inertia by the same
    // ratio is what Unity does, as far as we know (plan section 6.1).
    if (m.mass > 0) m.rotationalInertia *= mass / m.mass;
    m.mass = mass;
    b2Body_SetMassData(body, m);
}

// --- Events --------------------------------------------------------------

static bool is_dynamic(int c)
{
    return b2Body_GetType(colliders[c].body) == b2_dynamicBody;
}

static void dispatch(PhysicsEvent e, int a, int b)
{
    // Copies, since a handler may create colliders.
    Collider ca = colliders[a].info, cb = colliders[b].info;
    handler(e, &ca, &cb);
    handler(e, &cb, &ca);
}

static bool same_pair(const Touch *t, int a, int b, bool trigger)
{
    return t->trigger == trigger && ((t->colA == a && t->colB == b) || (t->colA == b && t->colB == a));
}

static int pair_touches(int a, int b, bool trigger)
{
    int n = 0;
    for (int i = 0; i < touchCount; i++) n += same_pair(&touches[i], a, b, trigger);
    return n;
}

static int find_touch(uint32_t ka, uint32_t kb, bool trigger)
{
    for (int i = 0; i < touchCount; i++) {
        const Touch *t = &touches[i];
        if (t->trigger == trigger && ((t->a == ka && t->b == kb) || (t->a == kb && t->b == ka))) return i;
    }
    return -1;
}

static void touch_begin(b2ShapeId sa, b2ShapeId sb, bool trigger)
{
    int a = shape_owner(sa), b = shape_owner(sb);
    if (a < 0 || b < 0 || a == b) return;
    // Unity 5.4's Box2D 2.x makes no contact, trigger or not, unless one
    // body is dynamic. No handler of the game reacts to such a pair anyway.
    if (!is_dynamic(a) && !is_dynamic(b)) return;
    // Two sensors that overlap both report it. Count the shape pair once.
    uint32_t ka = shape_key(sa), kb = shape_key(sb);
    if (find_touch(ka, kb, trigger) >= 0) return;
    if (touchCount == MAX_TOUCHES) {
        TraceLog(LOG_WARNING, "physics: more than %d touching pairs", MAX_TOUCHES);
        return;
    }
    bool first = pair_touches(a, b, trigger) == 0;
    touches[touchCount++] = (Touch){ka, kb, a, b, trigger};
    if (first) dispatch(trigger ? TRIGGER_ENTER : COLLISION_ENTER, a, b);
}

static void touch_end(b2ShapeId sa, b2ShapeId sb, bool trigger)
{
    int i = find_touch(shape_key(sa), shape_key(sb), trigger);
    if (i < 0) return; // never counted, or its body was destroyed already
    int a = touches[i].colA, b = touches[i].colB;
    touches[i] = touches[--touchCount];
    if (pair_touches(a, b, trigger) == 0) dispatch(trigger ? TRIGGER_EXIT : COLLISION_EXIT, a, b);
}

void physics_step(void)
{
    b2World_Step(world, FIXED_TIMESTEP, PHYSICS_SUB_STEPS);

    // Ends first, then begins. Unity's order between them is not known;
    // no handler of the game depends on it.
    b2SensorEvents se = b2World_GetSensorEvents(world);
    b2ContactEvents ce = b2World_GetContactEvents(world);
    for (int i = 0; i < se.endCount; i++) touch_end(se.endEvents[i].sensorShapeId, se.endEvents[i].visitorShapeId, true);
    for (int i = 0; i < ce.endCount; i++) touch_end(ce.endEvents[i].shapeIdA, ce.endEvents[i].shapeIdB, false);
    for (int i = 0; i < se.beginCount; i++) touch_begin(se.beginEvents[i].sensorShapeId, se.beginEvents[i].visitorShapeId, true);
    for (int i = 0; i < ce.beginCount; i++) touch_begin(ce.beginEvents[i].shapeIdA, ce.beginEvents[i].shapeIdB, false);
}

void physics_destroy_body(b2BodyId body)
{
    if (!b2Body_IsValid(body)) return;
    // Exits for every collider pair this body still touches.
    // ponytail: unverified on the reference whether Unity 5.4 sends these;
    // step 5 of the plan checks it with a rock destroyed in a placeholder.
    // Unity 5.6 added Physics2D.callbacksOnDisable, on by default, which
    // suggests that earlier versions sent them.
    typedef struct { int a, b; bool trigger; } Pair;
    static Pair exits[MAX_TOUCHES];
    int exitCount = 0;
    for (int i = 0; i < touchCount; i++) {
        const Touch *t = &touches[i];
        if (!B2_ID_EQUALS(colliders[t->colA].body, body) && !B2_ID_EQUALS(colliders[t->colB].body, body)) continue;
        bool seen = false;
        for (int k = 0; k < exitCount; k++) seen |= same_pair(t, exits[k].a, exits[k].b, exits[k].trigger);
        if (!seen) exits[exitCount++] = (Pair){t->colA, t->colB, t->trigger};
        touches[i--] = touches[--touchCount];
    }
    for (int k = 0; k < exitCount; k++) {
        dispatch(exits[k].trigger ? TRIGGER_EXIT : COLLISION_EXIT, exits[k].a, exits[k].b);
    }

    for (int c = 0; c < MAX_COLLIDERS; c++) {
        Slot *slot = &colliders[c];
        if (!slot->used || !B2_ID_EQUALS(slot->body, body)) continue;
        for (int s = 0; s < slot->shapeCount; s++) owners[slot->shapes[s].index1 - 1].collider = -1;
        slot->used = false;
    }
    // Box2D destroys the body's joints with it.
    b2DestroyBody(body);
}
