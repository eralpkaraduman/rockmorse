// physics: Unity's 2D physics as Rockmorse uses it, on Box2D v3.1.1.
//
// One Unity collider can be several Box2D shapes, because Box2D needs
// convex polygons of at most 8 points (plan section 6.1). The bridge keeps
// its own table from shapes to colliders, so events still find their
// collider after the shape is gone.
//
// Unity calls a trigger or collision handler on both objects of a pair. The
// bridge does the same, and counts per pair of Unity colliders: enter fires
// when the first pair of their shapes starts touching, exit when the last
// one stops. Triggers and collisions are counted apart.
#ifndef ROCKMORSE_PHYSICS_H
#define ROCKMORSE_PHYSICS_H

#include <stdbool.h>
#include "box2d/box2d.h"

// Box2D sub-steps per 0.02 s step. Unity 5.4 ran Box2D 2.x with 8 velocity
// and 3 position iterations instead. Box2D 3 suggests 4 sub-steps.
#define PHYSICS_SUB_STEPS 4

// A Unity collider, as its handlers see it. kind and index are the game's:
// what object owns it, and which one.
typedef struct {
    int kind;
    int index;
    const char *tag; // the GameObject tag, e.g. "Player"
    bool isTrigger;
} Collider;

typedef enum { TRIGGER_ENTER, TRIGGER_EXIT, COLLISION_ENTER, COLLISION_EXIT } PhysicsEvent;

// Called once per event and owner: self is the owner's collider, other the
// collider it touched.
typedef void (*PhysicsHandler)(PhysicsEvent event, const Collider *self, const Collider *other);

// Starts a world. Sets Box2D's length unit first, as Box2D requires.
void physics_init(PhysicsHandler handler);
void physics_shutdown(void);

// A body at a world position. Drag is Unity's Rigidbody2D drag. Fixed
// rotation goes in the body definition, because setting it later resets
// the mass.
b2BodyId physics_body(b2BodyType type, b2Vec2 position, float linearDrag, float angularDrag, float gravityScale,
                      bool fixedRotation);

// Colliders. Sizes are in world units, relative to the body origin.
// Each returns the collider's id.
int physics_circle(b2BodyId body, Collider c, b2Vec2 center, float radius);
int physics_box(b2BodyId body, Collider c, b2Vec2 center, b2Vec2 size);
// Convex pieces as values.h holds them: x, y pairs, pieces one after another.
int physics_polygon(b2BodyId body, Collider c, const float *points, const int *sizes, int pieces);
// The same pieces mirrored in x, for the player's half turn around Y.
// Contacts of the collider restart, triggers keep their overlaps.
void physics_polygon_mirror(int collider, const float *points, const int *sizes, int pieces, bool mirrored);

// Rigidbody2D.mass. Call it after all shapes, since each new shape would
// reset it. Inertia scales with the mass.
void physics_set_mass(b2BodyId body, float mass);

// Runs one fixed step and calls the handler for its events.
void physics_step(void);

// Destroys a body now. Its colliders first send exit events for every pair
// still touching, while the owner still exists (plan section 6.3, item 7).
// Box2D's own end events for it, a step later, are then ignored.
void physics_destroy_body(b2BodyId body);

b2WorldId physics_world(void);

// Box2D damping per sub-step that gives Unity's Box2D 2.x damping over one
// 0.02 s step: v *= 1 / (1 + h * drag).
float physics_damping(float unityDrag);

#endif
