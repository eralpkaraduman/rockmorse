// Rock.cs, on the RockPrefab. A rock is dark (a dash) or light (a dot). It
// spins while it moves, plays its drag sound, carries a shadow, and makes
// way when it touches anything tagged Rover.
#include "game.h"

Rock rocks[MAX_ROCKS];

// Rigidbody2D.AddTorque takes Unity's torque, which Box2D gets as it is.
// ponytail: step 4 of the plan checks the spin against the reference.
#define UNITY_TORQUE_TO_BOX2D 1.0f

Rock *rock_get(RockRef ref)
{
    if (ref.slot < 0 || ref.slot >= MAX_ROCKS) return NULL;
    Rock *r = &rocks[ref.slot];
    return r->alive && r->generation == ref.generation ? r : NULL;
}

RockRef rock_ref(const Rock *rock)
{
    return rock ? (RockRef){(int)(rock - rocks), rock->generation} : ROCK_NONE;
}

Vector2 rock_position(const Rock *rock)
{
    b2Vec2 p = b2Body_GetPosition(rock->body);
    return (Vector2){p.x, p.y};
}

// original: // Use this for initialization
static void Start(Rock *r)
{
    // dragSFXSource = GetComponent("AudioSource"): the rock's own source.
    // Instantiate(rockShadowPrefab, position, identity), parented to the
    // rock's parent.
    r->shadow = SPRITE_RENDERER(ROCK_SHADOW);
    r->shadow.transform.position = r->renderer.transform.position;
    r->shadowAlive = true;
    r->shadowDestroyQueued = false;
    // original: //isDragTarget (false);
}

// original: /*
// original: 	public void isDragTarget(bool value){
// original: 		targetIndicatorSpriteRenderer.enabled = value;
// original: 	}
// original: 	*/

// original: /*
// original: 	public void setFixed(bool isFixed){
// original: 		transform.rigidbody2D.isKinematic = isFixed;
// original: 	}
// original: 	*/

// Unity calls Start before the rock's first Update. Nothing moves the
// rock in between, so the port calls it right away.
Rock *rock_instantiate(Vector3 position)
{
    int slot = -1;
    for (int i = 0; i < MAX_ROCKS && slot < 0; i++) {
        if (!rocks[i].alive && !rocks[i].destroyQueued) slot = i;
    }
    if (slot < 0) {
        TraceLog(LOG_WARNING, "more than %d rocks", MAX_ROCKS);
        return NULL;
    }
    Rock *r = &rocks[slot];
    unsigned generation = r->generation;
    *r = (Rock){.alive = true, .generation = generation};
    r->body = physics_body(b2_dynamicBody, (b2Vec2){position.x, position.y}, ROCK_RB_LINEAR_DRAG,
                           ROCK_RB_ANGULAR_DRAG, ROCK_RB_GRAVITY_SCALE, ROCK_RB_FIXED_ANGLE);
    r->collider = physics_circle(r->body, (Collider){OWNER_ROCK, slot, TAG_UNTAGGED, ROCK_IS_TRIGGER},
                                 (b2Vec2){ROCK_OFFSET_WORLD_X, ROCK_OFFSET_WORLD_Y}, ROCK_RADIUS_WORLD);
    physics_set_mass(r->body, ROCK_RB_MASS);
    r->renderer = SPRITE_RENDERER(ROCK);
    r->renderer.transform.position = position;
    r->dragSFXSource = AUDIO_SOURCE(ROCK_DRAG);
    audio_source_awake(&r->dragSFXSource);
    r->jointEnabled = ROCK_JOINT_ENABLED;
    r->jointConnected = false;
    r->joint = b2_nullJointId;
    Start(r);
    return r;
}

void rock_set_dark(Rock *r, bool value)
{
    r->renderer.sprite = value ? ROCK_DARK_SPRITE : ROCK_LIGHT_SPRITE;
    r->dark = value;
}

void rock_set_position(Rock *r, Vector3 position)
{
    r->renderer.transform.position = position;
    b2Body_SetTransform(r->body, (b2Vec2){position.x, position.y}, b2Body_GetRotation(r->body));
}

void rock_set_joint(Rock *r, bool enabled, bool connected)
{
    r->jointEnabled = enabled;
    r->jointConnected = connected;
    bool want = enabled && connected;
    if (want && B2_IS_NULL(r->joint)) {
        b2DistanceJointDef def = b2DefaultDistanceJointDef();
        def.bodyIdA = r->body;
        def.bodyIdB = player.body;
        def.localAnchorA = (b2Vec2){ROCK_JOINT_ANCHOR_X, ROCK_JOINT_ANCHOR_Y};
        def.localAnchorB = (b2Vec2){ROCK_JOINT_CONNECTED_ANCHOR_X, ROCK_JOINT_CONNECTED_ANCHOR_Y};
        def.length = ROCK_JOINT_DISTANCE;
        // Unity's DistanceJoint2D has no spring: rigid, unless max
        // distance only, which it is not.
        def.enableSpring = false;
        def.enableLimit = ROCK_JOINT_MAX_DISTANCE_ONLY;
        def.collideConnected = ROCK_JOINT_COLLIDE_CONNECTED;
        r->joint = b2CreateDistanceJoint(physics_world(), &def);
    } else if (!want && !B2_IS_NULL(r->joint)) {
        b2DestroyJoint(r->joint);
        r->joint = b2_nullJointId;
    }
}

// original: // Update is called once per frame
void rock_update(Rock *r)
{

    b2Vec2 velocity = b2Body_GetLinearVelocity(r->body);
    float hVelocity = velocity.x;
    float vVelocity = velocity.y;

    if (fabsf(hVelocity) >= ROCK_SFX_MIN_VELOCITY ||
        fabsf(vVelocity) >= ROCK_SFX_MIN_VELOCITY) {

        audio_source_set_enabled(&r->dragSFXSource, true);
        float t = hVelocity;
        if (fabsf(vVelocity) > fabsf(hVelocity)) {
            t = -vVelocity;
        }

        // original: //t = Mathf.Max(hVelocity,vVelocity);
        // Torque adds up until the next physics step, as in Unity. Update
        // runs 60 times a second and physics 50, so some steps get two.
        b2Body_ApplyTorque(r->body, -t * ROCK_TORQUE_FACTOR * UNITY_TORQUE_TO_BOX2D, true);

    } else {
        audio_source_set_enabled(&r->dragSFXSource, false);
    }

    Vector3 shadowPosition = r->renderer.transform.position;
    shadowPosition.y -= ROCK_SHADOW_OFFSET_Y;
    shadowPosition.z = ROCK_SHADOW_Z;
    r->shadow.transform.position = shadowPosition;

}

void rock_on_event(Rock *r, PhysicsEvent e, const Collider *other)
{
    // Unity also calls OnCollisionStay2D and OnTriggerStay2D, which do the
    // same against the rover. The bridge has no stay events: the enter
    // event already relocates the rock, which is then destroyed.
    if (e == COLLISION_ENTER) { // OnCollisionEnter2D(Collision2D o)
        if (strcmp(other->tag, ROCK_REPELLER_TAG) == 0) {
            rock_relocate(r);
        }
        // original: /*
        // original: 		if ( == "Rover") {
        // original: 			Debug.Log("rv");
        // original: 			renderer.enabled = false;
        // original: 		}
        // original: 		*/
    } else if (e == TRIGGER_ENTER) { // OnTriggerEnter2D(Collider2D o)
        if (strcmp(other->tag, ROCK_REPELLER_TAG) == 0) {
            rock_relocate(r);
        }
        // original: /*
        // original: 		if ( == "Rover") {
        // original: 			Debug.Log("rv");
        // original: 			renderer.enabled = false;
        // original: 		}
        // original: 		*/
    }
}

void rock_relocate(Rock *r)
{

    b2Body_SetLinearVelocity(r->body, b2Vec2_zero);

    // DistanceJoint2D dst. Clearing connectedBody leaves the joint enabled
    // but unconnected, which in Unity ties the rock to the world. The rock
    // is destroyed at the end of this frame, before another physics step,
    // so the port simply drops the Box2D joint.
    if (r->jointConnected) {

        rockDragger.activeJoint = ROCK_NONE; // rd.activeJoint = null
        rock_set_joint(r, r->jointEnabled, false);
    }

    if (r->shadowAlive) {
        r->shadowDestroyQueued = true; // Destroy(this.shadow.gameObject)
    }
    rock_generator_replace(r);

    // original: //this.transform.position = rg.randomPositon();

    // original: //float forceX = -transform.rigidbody2D.velocity.x*2;
    // original: //float forceY = -transform.rigidbody2D.velocity.y*2;

    // original: //transform.rigidbody2D.AddTorque (-forceX*20);
    // original: //transform.rigidbody2D.AddForce (new Vector2 (forceX,forceY));
    // original: //transform.rigidbody2D.velocity = new Vector2 (forceX,forceY);
}

void rock_destroy(Rock *r)
{
    r->destroyQueued = true;
}

// Unity's deferred Destroy. A rock queued twice is freed once.
void rocks_end_of_frame(void)
{
    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &rocks[i];
        if (r->shadowDestroyQueued) {
            r->shadowAlive = false;
            r->shadowDestroyQueued = false;
        }
        if (!r->destroyQueued) continue;
        // Exit events go out first, while the rock still exists.
        physics_destroy_body(r->body);
        audio_source_destroy(&r->dragSFXSource);
        r->joint = b2_nullJointId; // Box2D destroyed it with the body
        r->alive = false;
        r->destroyQueued = false;
        r->generation++;
    }
}

// Transforms follow the bodies after each physics step.
void rocks_sync(void)
{
    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &rocks[i];
        if (!r->alive) continue;
        b2Transform t = b2Body_GetTransform(r->body);
        r->renderer.transform.position.x = t.p.x;
        r->renderer.transform.position.y = t.p.y;
        r->renderer.transform.angle = b2Rot_GetAngle(t.q) * RAD2DEG;
    }
}

void rocks_sprites(SpriteList *list)
{
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (!rocks[i].alive) continue;
        sprite_list_add(list, &rocks[i].renderer);
        if (rocks[i].shadowAlive) sprite_list_add(list, &rocks[i].shadow);
    }
}
