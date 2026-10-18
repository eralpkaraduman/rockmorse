// TopDownControls.cs and the Game scene's Player object: the astronaut.
// It walks with the Horizontal and Vertical axes, faces where it walks,
// plays footsteps and picks its animation, and stays inside the world.
#include "game.h"

Player player;

// The body polygon is mirrored while the player faces left.
static bool bodyMirrored;

// original: // Normal Movements Variables
// (walkSpeed, sprintSpeed, curSpeed and maxSpeed are in Player.)

static void Start(void)
{
    float Agility = PLAYER_AGILITY;
    float Speed = PLAYER_SPEED;

    player.walkSpeed = (float)(Speed + (Agility / PLAYER_AGILITY_DIVISOR));
    player.sprintSpeed = player.walkSpeed + (player.walkSpeed / 2);

    player.walkSpeed *= PLAYER_WALK_SPEED_FACTOR;


}

void player_start(void)
{
    player = (Player){
        .renderer = SPRITE_RENDERER(PLAYER),
        .animator = animator_new(),
        .footStepsSFX = AUDIO_SOURCE(PLAYER_FOOTSTEPS),
        .strainSFX = AUDIO_SOURCE(PLAYER_STRAIN),
        .ambience = AUDIO_SOURCE(PLAYER_AMBIENCE),
    };
    bodyMirrored = false;
    player.body = physics_body(PLAYER_RB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody,
                               (b2Vec2){PLAYER_POS_X, PLAYER_POS_Y}, PLAYER_RB_LINEAR_DRAG, PLAYER_RB_ANGULAR_DRAG,
                               PLAYER_RB_GRAVITY_SCALE, PLAYER_RB_FIXED_ANGLE);
    player.triggerCollider = physics_circle(player.body, (Collider){OWNER_PLAYER, 0, PLAYER_TAG, PLAYER_TRIGGER_IS_TRIGGER},
                                            (b2Vec2){PLAYER_TRIGGER_OFFSET_WORLD_X, PLAYER_TRIGGER_OFFSET_WORLD_Y},
                                            PLAYER_TRIGGER_RADIUS_WORLD);
    player.bodyCollider = physics_polygon(player.body, (Collider){OWNER_PLAYER, 0, PLAYER_TAG, PLAYER_BODY_IS_TRIGGER},
                                          PLAYER_BODY_PIECES_POINTS, PLAYER_BODY_PIECES_SIZES, PLAYER_BODY_PIECES_COUNT);
    physics_set_mass(player.body, PLAYER_RB_MASS);
    audio_source_awake(&player.footStepsSFX);
    audio_source_awake(&player.strainSFX);
    audio_source_awake(&player.ambience);
    Start();
}

Vector3 player_position(void)
{
    return player.renderer.transform.position;
}

void player_fixed_update(void)
{
    player.curSpeed = player.walkSpeed;
    player.maxSpeed = player.curSpeed;

    // original: // Move senteces
    b2Body_SetLinearVelocity(player.body,
                             (b2Vec2){mathf_lerp(0, input_get_axis(AXIS_HORIZONTAL) * player.curSpeed, PLAYER_VELOCITY_LERP_T),
                                      mathf_lerp(0, input_get_axis(AXIS_VERTICAL) * player.curSpeed, PLAYER_VELOCITY_LERP_T)});

    bool dragging = false;
    // dragger = GetComponent("RockDragger"): the Player's RockDragger.
    if (rock_get(rockDragger.activeJoint) != NULL) {
        player.footStepsSFX.pitch = PLAYER_FOOTSTEP_PITCH_DRAGGING;
        dragging = true;
    } else {
        player.footStepsSFX.pitch = PLAYER_FOOTSTEP_PITCH_WALKING;
    }

    float velocityX = b2Body_GetLinearVelocity(player.body).x;
    float velocityY = b2Body_GetLinearVelocity(player.body).y;

    if (fabsf(velocityX) >= PLAYER_SFX_MIN_VELOCITY ||
        fabsf(velocityY) >= PLAYER_SFX_MIN_VELOCITY) {

        audio_source_set_enabled(&player.footStepsSFX, true);

        // The script writes 180 or 0 straight into the y component of the
        // rotation quaternion. Unity normalises it into a half turn around
        // Y (179.4 degrees), which mirrors the sprite, its children and its
        // colliders. Walking straight up or down also faces left (vx is 0).
        bool facingLeft = velocityX <= PLAYER_FACING_LEFT_MAX_VELOCITY_X;
        player.renderer.transform.flipped = facingLeft;
        if (facingLeft != bodyMirrored) {
            // ponytail: mirroring the polygon follows Unity's documented
            // collider geometry, not a measurement.
            physics_polygon_mirror(player.bodyCollider, PLAYER_BODY_PIECES_POINTS, PLAYER_BODY_PIECES_SIZES,
                                   PLAYER_BODY_PIECES_COUNT, facingLeft);
            bodyMirrored = facingLeft;
        }


        // The listener turns the other way (plan section 6.3, item 5).
        player.listenerFacingLeft = !facingLeft;


        animator_play(&player.animator, dragging ? PLAYER_ANIM_DRAG : PLAYER_ANIM_RUN);


    } else {
        audio_source_set_enabled(&player.footStepsSFX, false);

        animator_play(&player.animator, PLAYER_ANIM_IDLE);
    }


    float halfW = mars.worldBunds.width / 2 - PLAYER_CLAMP_MARGIN_X;
    float halfH = mars.worldBunds.height / 2 - PLAYER_CLAMP_MARGIN_Y;
    Vector3 pos = player_position();
    pos.x = mathf_clamp(pos.x, -halfW, halfW);
    pos.y = mathf_clamp(pos.y, -halfH, halfH);
    // this.transform.position = pos. A script-set position moves the body.
    // Only a clamp changes it, so the port moves the body only then.
    if (pos.x != player.renderer.transform.position.x || pos.y != player.renderer.transform.position.y) {
        b2Body_SetTransform(player.body, (b2Vec2){pos.x, pos.y}, b2Body_GetRotation(player.body));
        player.renderer.transform.position = pos;
    }

}

// The transform follows the body after each physics step.
void player_sync(void)
{
    b2Vec2 p = b2Body_GetPosition(player.body);
    player.renderer.transform.position.x = p.x;
    player.renderer.transform.position.y = p.y;
}
