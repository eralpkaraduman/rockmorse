// RockDragger.cs, on the Player. Rocks inside the player's big trigger
// circle are pull candidates. Space (or gamepad A) on the nearest one ties
// it to the player with its distance joint, until Space goes up.
#include "game.h"

RockDragger rockDragger;

static void startPulling(void);
static void release(void);

// original: // Use this for initialization
void rock_dragger_start(void)
{
    rockDragger = (RockDragger){.pullCandidate = ROCK_NONE, .activeJoint = ROCK_NONE};
}

// A destroyed rock reads as null, as in Unity, so it is skipped.
static RockRef closestCandidate(void)
{

    float closestDistance = INFINITY;
    RockRef closest = ROCK_NONE;
    for (int i = 0; i < rockDragger.pullCandidateCount; i++) {
        const Rock *rock = rock_get(rockDragger.pullCandidates[i]);
        if (rock == NULL) continue;

        Vector2 rock_pos = rock_position(rock);
        Vector3 p = player_position();
        Vector2 player_pos = {p.x, p.y};
        float d = sqrtf((rock_pos.x - player_pos.x) * (rock_pos.x - player_pos.x) +
                        (rock_pos.y - player_pos.y) * (rock_pos.y - player_pos.y)); // Vector2.Distance
        if (d < closestDistance) {
            closest = rockDragger.pullCandidates[i];
            closestDistance = d;
        }
    }

    return closest;
}

// original: // Update is called once per frame
void rock_dragger_update(void)
{

    rockDragger.pullCandidate = closestCandidate();

    if (input_get_key_down(KEY_SPACE)) {



        if (rock_get(rockDragger.pullCandidate) != NULL && rock_get(rockDragger.activeJoint) == NULL) {
            startPulling();
        }

    }

    if (input_get_key_up(KEY_SPACE)) {
        if (rock_get(rockDragger.activeJoint)) {
            release();
        }
    }

    if (rock_get(rockDragger.activeJoint) != NULL) {
        dragTarget.target = rockDragger.activeJoint;
    } else if (rock_get(rockDragger.pullCandidate) != NULL) {
        dragTarget.target = rockDragger.pullCandidate;
    } else {
        dragTarget.target = ROCK_NONE;
    }
}

static void startPulling(void)
{

    Rock *dst = rock_get(rockDragger.pullCandidate);

    // dst.connectedBody = player body; activeJoint = dst; dst.enabled = true
    rockDragger.activeJoint = rockDragger.pullCandidate;
    rock_set_joint(dst, true, true);

    // The Player's AudioSource is disabled in the scene and nothing
    // enables it, so this plays nothing (INVENTORY.md section 2.5). The 2016
    // WebGL build logs "Can not play a disabled audio source" here.
    audio_source_play(&player.strainSFX);

    // original: //pullCandidate.transform.GetComponent<Rock> ().setFixed(false);
}

static void release(void)
{
    rock_set_joint(rock_get(rockDragger.activeJoint), false, false);
    rockDragger.activeJoint = ROCK_NONE;

    b2Body_SetLinearVelocity(player.body, b2Vec2_zero);

}

// OnTriggerEnter2D and OnTriggerExit2D of the Player, for either of its
// colliders. Only the trigger circle meets rocks this way: the body is
// solid, and so are rocks.
void rock_dragger_on_event(PhysicsEvent e, const Collider *o)
{
    if (e == TRIGGER_ENTER) {
        // Rock r = o.GetComponent<Rock>()
        if (o->kind == OWNER_ROCK) {

            // No duplicate check, as in the source.
            if (rockDragger.pullCandidateCount < MAX_PULL_CANDIDATES) {
                rockDragger.pullCandidates[rockDragger.pullCandidateCount++] = rock_ref(&rocks[o->index]);
            }

            // original: /*
            // original: 			if(pullCandidate!=null){
            // original: 
            // original: 
            // original: 				Vector2 oldc_pos = pullCandidate.transform.position;
            // original: 				Vector2 player_pos = transform.position;
            // original: 				Vector2 newc_pos = o.transform.position;
            // original: 
            // original: 				float d1 = Vector2.Distance(oldc_pos,player_pos);
            // original: 				float d2 = Vector2.Distance(newc_pos,player_pos);
            // original: 
            // original: 				if(d1<d2){
            // original: 					pullCandidate = o;
            // original: 				}
            // original: 
            // original: 
            // original: 			}else{
            // original: 				pullCandidate = o;
            // original: 			}
            // original: 			*/

            // original: //pullCandidate = o;
            // original: //swapDragTarget(r);


        }

    } else if (e == TRIGGER_EXIT) {

        // pullCandidates.Remove(o): the first entry for that collider.
        if (o->kind == OWNER_ROCK) {
            RockRef ref = rock_ref(&rocks[o->index]);
            for (int i = 0; i < rockDragger.pullCandidateCount; i++) {
                if (rockDragger.pullCandidates[i].slot != ref.slot ||
                    rockDragger.pullCandidates[i].generation != ref.generation) continue;
                for (int k = i + 1; k < rockDragger.pullCandidateCount; k++) {
                    rockDragger.pullCandidates[k - 1] = rockDragger.pullCandidates[k];
                }
                rockDragger.pullCandidateCount--;
                break;
            }
        }

        // original: /*
        // original: 		if(pullCandidate == o){
        // original: 			pullCandidate = null;
        // original: 		}
        // original: 		*/

    }
}
// original: /*
// original: 	void swapDragTarget(Rock r){
// original: 		r.isDragTarget(true);
// original: 		if(pullCandidate!=null){
// original: 			Rock r_old = pullCandidate.transform.GetComponent<Rock> ();
// original: 			Debug.Log("unselect");
// original: 			r_old.isDragTarget(false);
// original: 		}
// original: 
// original: 	}
// original: 	*/

// RockDragger.releaseIfDragging has no caller, so the port leaves it out.
