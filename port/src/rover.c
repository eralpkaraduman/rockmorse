// The Game scene's Rover and its scripts: Rover.cs on RoverBody shows the
// roof while the player's body is under it, and RoverInsideTrigger.cs on
// playerInsideRoverTrigger turns the rover screen on and stops the sol
// clock while the player is inside. The rock repeller, a big trigger tagged
// Rover, makes rocks relocate before they reach the rover.
#include "game.h"

static SpriteRenderer roverBody, roof;
SpriteRenderer missionUpdateIndicator;
static AudioSource enterSound; // playerInsideRoverTrigger's AudioSource
static AudioSource chatter;    // playerInsideRoverTrigger/chatter, the radio static

// The chatter's world position is not in values.h yet. These are the
// dump's values, scenes/Game.json#Rover/playerInsideRoverTrigger/chatter:
// @transform:world_position, used until extract.py adds the row (step 10).
#ifndef ROVER_CHATTER_POS_X
#define ROVER_CHATTER_POS_X 9.186773f
#define ROVER_CHATTER_POS_Y 4.7684665f
#define ROVER_CHATTER_POS_Z -0.5f
#endif

// RoverInsideTrigger.cs fields.
static float setOnPitch;
static float setOffPitch;

// The Rover's own AudioSource is the newMessage loop. It plays from the
// start at volume 0, and Earth turns it up while a message waits. The radio
// static loops from the start too; its linear rolloff (0.42 to 4.6) makes
// it heard only near the rover. All three are 3D and never move.
// Rover/Reverb, an AudioReverbZone, is left out (port/README.md).

void rover_start(void)
{
    roverBody = SPRITE_RENDERER(ROVER_BODY);
    roof = SPRITE_RENDERER(ROVER_ROOF);
    missionUpdateIndicator = SPRITE_RENDERER(ROVER_UPDATES_INDICATOR);
    enterSound = AUDIO_SOURCE(ROVER_ENTER_SOUND);
    enterSound.position = (Vector3){ROVER_INSIDE_TRIGGER_POS_X, ROVER_INSIDE_TRIGGER_POS_Y, ROVER_INSIDE_TRIGGER_POS_Z};
    audio_source_awake(&enterSound);
    roverNewMessageSound = AUDIO_SOURCE(ROVER_NEW_MESSAGE);
    roverNewMessageSound.position = (Vector3){ROVER_POS_X, ROVER_POS_Y, ROVER_POS_Z};
    audio_source_awake(&roverNewMessageSound);
    chatter = AUDIO_SOURCE(ROVER_CHATTER);
    chatter.position = (Vector3){ROVER_CHATTER_POS_X, ROVER_CHATTER_POS_Y, ROVER_CHATTER_POS_Z};
    audio_source_awake(&chatter);
    setOnPitch = ROVER_INSIDE_SET_ON_PITCH;
    setOffPitch = ROVER_INSIDE_SET_OFF_PITCH;

    // Rover/RoverBody: a kinematic body with the box trigger.
    b2BodyId body = physics_body(ROVER_BODY_RB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody,
                                 (b2Vec2){ROVER_BODY_POS_X, ROVER_BODY_POS_Y}, ROVER_BODY_RB_LINEAR_DRAG,
                                 ROVER_BODY_RB_ANGULAR_DRAG, ROVER_BODY_RB_GRAVITY_SCALE, ROVER_BODY_RB_FIXED_ANGLE);
    physics_box(body, (Collider){OWNER_ROVER_BODY, 0, ROVER_BODY_TAG, ROVER_BODY_TRIGGER_IS_TRIGGER},
                (b2Vec2){ROVER_BODY_TRIGGER_OFFSET_WORLD_X, ROVER_BODY_TRIGGER_OFFSET_WORLD_Y},
                (b2Vec2){ROVER_BODY_TRIGGER_SIZE_WORLD_X, ROVER_BODY_TRIGGER_SIZE_WORLD_Y});

    // Rover/bis_rover_v2_roof: a kinematic body with the solid roof polygon.
    body = physics_body(ROVER_ROOF_RB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody,
                        (b2Vec2){ROVER_ROOF_POS_X, ROVER_ROOF_POS_Y}, ROVER_ROOF_RB_LINEAR_DRAG,
                        ROVER_ROOF_RB_ANGULAR_DRAG, ROVER_ROOF_RB_GRAVITY_SCALE, ROVER_ROOF_RB_FIXED_ANGLE);
    physics_polygon(body, (Collider){OWNER_ROVER_ROOF, 0, ROVER_ROOF_TAG, ROVER_ROOF_COLLIDER_IS_TRIGGER},
                    ROVER_ROOF_COLLIDER_PIECES_POINTS, ROVER_ROOF_COLLIDER_PIECES_SIZES, ROVER_ROOF_COLLIDER_PIECES_COUNT);

    // Rover/rockRepeller and Rover/playerInsideRoverTrigger have no
    // Rigidbody2D, so they are static.
    body = physics_body(b2_staticBody, (b2Vec2){ROCK_REPELLER_POS_X, ROCK_REPELLER_POS_Y}, 0, 0, 0, false);
    physics_circle(body, (Collider){OWNER_ROVER_REPELLER, 0, ROCK_REPELLER_TAG, ROCK_REPELLER_IS_TRIGGER},
                   (b2Vec2){ROCK_REPELLER_OFFSET_WORLD_X, ROCK_REPELLER_OFFSET_WORLD_Y}, ROCK_REPELLER_RADIUS_WORLD);
    body = physics_body(b2_staticBody, (b2Vec2){ROVER_INSIDE_TRIGGER_POS_X, ROVER_INSIDE_TRIGGER_POS_Y}, 0, 0, 0, false);
    physics_circle(body, (Collider){OWNER_ROVER_INSIDE, 0, TAG_UNTAGGED, ROVER_INSIDE_TRIGGER_IS_TRIGGER},
                   (b2Vec2){ROVER_INSIDE_TRIGGER_OFFSET_WORLD_X, ROVER_INSIDE_TRIGGER_OFFSET_WORLD_Y},
                   ROVER_INSIDE_TRIGGER_RADIUS_WORLD);
}

Vector3 rover_focus_target(void)
{
    return (Vector3){ROVER_FOCUS_TARGET_POS_X, ROVER_FOCUS_TARGET_POS_Y, ROVER_FOCUS_TARGET_POS_Z};
}

// Rover.cs. isTrigger == false picks the player's body polygon.
// original: // Use this for initialization
// original: // Update is called once per frame
// (Rover.Start and Rover.Update are empty.)
static void Rover_OnTrigger(PhysicsEvent e, const Collider *o)
{
    if (e == TRIGGER_ENTER) {

        if (strcmp(o->tag, ROVER_TRIGGER_PLAYER_TAG) == 0 && o->isTrigger == false) {
            // original: //focusSwitcher.playerIsInRover = true;
            roof.enabled = true;
        }
    } else if (e == TRIGGER_EXIT) {

        if (strcmp(o->tag, ROVER_TRIGGER_PLAYER_TAG) == 0 && o->isTrigger == false) {
            // original: //focusSwitcher.playerIsInRover = false;
            roof.enabled = false;
        }
    }
}

// RoverInsideTrigger.cs. Either collider of the player counts, so the big
// trigger circle alone turns the screen on, and either one leaving turns it
// off (plan section 6.3, item 1).
// original: // Use this for initialization
// original: //audioSource = GetComponent<AudioSource> ();
// original: // Update is called once per frame
// (RoverInsideTrigger.Start and Update are empty.)
static void RoverInsideTrigger_OnTrigger(PhysicsEvent e, const Collider *o)
{
    if (e == TRIGGER_ENTER) {

        if (strcmp(o->tag, ROVER_INSIDE_PLAYER_TAG) == 0 && focus.playerIsInRover == false) {

            // The scene's pitch of -0.88 never plays: it is set to 0.6 or
            // 0.9 before every Play. Measured on the 2016 WebGL build: the
            // enter sound starts with playbackRate 0.6.
            audio_source_stop(&enterSound);
            enterSound.pitch = setOnPitch;
            audio_source_play(&enterSound);


            focus.playerIsInRover = true;
            rover_screen_set_on(true);

            mars.timeFrozen = true;
        }
    } else if (e == TRIGGER_EXIT) {

        if (strcmp(o->tag, ROVER_INSIDE_PLAYER_TAG) == 0 && focus.playerIsInRover == true) {



            focus.playerIsInRover = false;
            rover_screen_set_on(false);

            audio_source_stop(&enterSound);
            enterSound.pitch = setOffPitch;
            audio_source_play(&enterSound);

            mars.timeFrozen = false;

        }
    }
}

void rover_on_event(Owner owner, PhysicsEvent e, const Collider *other)
{
    if (owner == OWNER_ROVER_BODY) Rover_OnTrigger(e, other);
    else if (owner == OWNER_ROVER_INSIDE) RoverInsideTrigger_OnTrigger(e, other);
}

void rover_sprites(SpriteList *list)
{
    sprite_list_add(list, &roverBody);
    sprite_list_add(list, &roof);
    sprite_list_add(list, &missionUpdateIndicator);
}
