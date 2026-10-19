// The RocketPrefab and its scripts: Rocket.cs, InsideRocketTrigger.cs on
// its InsideTrigger, and VisibleOnTrigger.cs on its RocketCover. Earth puts
// the rocket down after the last message. Walking into it wins.
#include "game.h"

static bool spawned;
static SpriteRenderer rocketBody, rocketCover;

void rocket_reset(void)
{
    spawned = false;
}

// Instantiate(rocketPrefab) without a position: the prefab's own. Its
// AudioReverbZone is left out, as the rover's is (port/README.md).
void rocket_spawn(void)
{
    spawned = true;
    rocketBody = SPRITE_RENDERER(ROCKET_BODY);
    rocketCover = SPRITE_RENDERER(ROCKET_COVER);

    // RocketBody: kinematic, with the solid body polygon.
    b2BodyId body = physics_body(ROCKET_BODY_RB_KINEMATIC ? b2_kinematicBody : b2_dynamicBody,
                                 (b2Vec2){ROCKET_BODY_POS_X, ROCKET_BODY_POS_Y}, ROCKET_BODY_RB_LINEAR_DRAG,
                                 ROCKET_BODY_RB_ANGULAR_DRAG, ROCKET_BODY_RB_GRAVITY_SCALE, ROCKET_BODY_RB_FIXED_ANGLE);
    physics_polygon(body, (Collider){OWNER_ROCKET_BODY, 0, ROCKET_BODY_TAG, ROCKET_BODY_COLLIDER_IS_TRIGGER},
                    ROCKET_BODY_COLLIDER_PIECES_POINTS, ROCKET_BODY_COLLIDER_PIECES_SIZES,
                    ROCKET_BODY_COLLIDER_PIECES_COUNT);

    // The cover, the rock repeller and the inside trigger have no
    // Rigidbody2D, so they are static.
    body = physics_body(b2_staticBody, (b2Vec2){ROCKET_COVER_POS_X, ROCKET_COVER_POS_Y}, 0, 0, 0, false);
    physics_box(body, (Collider){OWNER_ROCKET_COVER, 0, ROCKET_COVER_TAG, ROCKET_COVER_TRIGGER_IS_TRIGGER},
                (b2Vec2){ROCKET_COVER_TRIGGER_OFFSET_WORLD_X, ROCKET_COVER_TRIGGER_OFFSET_WORLD_Y},
                (b2Vec2){ROCKET_COVER_TRIGGER_SIZE_WORLD_X, ROCKET_COVER_TRIGGER_SIZE_WORLD_Y});
    body = physics_body(b2_staticBody, (b2Vec2){ROCKET_REPELLER_POS_X, ROCKET_REPELLER_POS_Y}, 0, 0, 0, false);
    physics_box(body, (Collider){OWNER_ROCKET_REPELLER, 0, ROCKET_REPELLER_TAG, ROCKET_REPELLER_IS_TRIGGER},
                (b2Vec2){ROCKET_REPELLER_OFFSET_WORLD_X, ROCKET_REPELLER_OFFSET_WORLD_Y},
                (b2Vec2){ROCKET_REPELLER_SIZE_WORLD_X, ROCKET_REPELLER_SIZE_WORLD_Y});
    body = physics_body(b2_staticBody, (b2Vec2){ROCKET_INSIDE_TRIGGER_POS_X, ROCKET_INSIDE_TRIGGER_POS_Y}, 0, 0, 0, false);
    physics_box(body, (Collider){OWNER_ROCKET_INSIDE, 0, ROCKET_INSIDE_TRIGGER_TAG, ROCKET_INSIDE_TRIGGER_IS_TRIGGER},
                (b2Vec2){ROCKET_INSIDE_TRIGGER_OFFSET_WORLD_X, ROCKET_INSIDE_TRIGGER_OFFSET_WORLD_Y},
                (b2Vec2){ROCKET_INSIDE_TRIGGER_SIZE_WORLD_X, ROCKET_INSIDE_TRIGGER_SIZE_WORLD_Y});
}

// Rocket.cs
// original: // Use this for initialization
// original: // Update is called once per frame
// (Rocket.Start and Rocket.Update are empty.)
static void playerJustGitInRocket(void) // spelling as in the source
{

    scene_load_name(ROCKET_WIN_SCENE);
}

// InsideRocketTrigger.cs. Either collider of the player wins.
// original: // Use this for initialization
// original: // Update is called once per frame
// (InsideRocketTrigger.Start and Update are empty.)
static void InsideRocketTrigger_OnTriggerEnter2D(const Collider *o)
{

    if (strcmp(o->tag, ROCKET_INSIDE_PLAYER_TAG) == 0) {

        // this.transform.parent.GetComponent<Rocket>()
        playerJustGitInRocket();
        // original: //this.transform.GetComponent<Rocket> ().playerJustGitInRocket ();
        // original: //focusSwitcher.playerIsInRover = true;
        // original: //roverScreen.setOn(true);
    }
}
// original: /*
// original: 	void OnTriggerExit2D(Collider2D o){
// original: 		
// original: 		if (o.tag == "Player") {
// original: 			focusSwitcher.playerIsInRover = false;
// original: 			roverScreen.setOn(false);
// original: 		}
// original: 	}
// original: 	*/

// VisibleOnTrigger.cs on RocketCover. isTrigger == false picks the
// player's body polygon.
// original: // Use this for initialization
// original: // Update is called once per frame
// (VisibleOnTrigger.Start takes its own SpriteRenderer; Update is empty.)
static void VisibleOnTrigger_OnTrigger(PhysicsEvent e, const Collider *o)
{
    if (e == TRIGGER_ENTER) {

        if (strcmp(o->tag, VISIBLE_ON_TRIGGER_PLAYER_TAG) == 0 && o->isTrigger == false) {
            // original: //focusSwitcher.playerIsInRover = true;
            rocketCover.enabled = true;
        }
    } else if (e == TRIGGER_EXIT) {

        if (strcmp(o->tag, VISIBLE_ON_TRIGGER_PLAYER_TAG) == 0 && o->isTrigger == false) {
            // original: //focusSwitcher.playerIsInRover = false;
            rocketCover.enabled = false;
        }
    }
}

void rocket_on_event(Owner owner, PhysicsEvent e, const Collider *other)
{
    if (owner == OWNER_ROCKET_INSIDE && e == TRIGGER_ENTER) InsideRocketTrigger_OnTriggerEnter2D(other);
    else if (owner == OWNER_ROCKET_COVER) VisibleOnTrigger_OnTrigger(e, other);
}

void rocket_sprites(SpriteList *list)
{
    if (!spawned) return;
    sprite_list_add(list, &rocketBody);
    sprite_list_add(list, &rocketCover);
}
