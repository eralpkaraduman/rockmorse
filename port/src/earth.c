// Earth.cs, on the Game scene's Earth object: the message flow.
// Only part of it is here yet: the first tutorial at the start, Space to
// dismiss a cutscene, and the update indicator. Decoding, new messages, the
// tutorial keys and the last level come in step 5 of the plan.
#include "game.h"

// Earth.cs private fields used so far.
static bool willLoadNewLevel = false; // original: // should enter rover, wont decode as message is old
static float solMark = INFINITY;

static void showCutscene(SpriteRenderer *newMessage);

// original: // Use this for initialization
void earth_start(void)
{
    willLoadNewLevel = false;
    solMark = INFINITY;

    Rgba c = missionUpdateIndicator.color;
    c.a = EARTH_INDICATOR_START_ALPHA;
    missionUpdateIndicator.color = c;

    showCutscene(&focus.tutorial);
    audio_listener_set_volume(EARTH_START_AUDIO_VOLUME);
}

void earth_new_sol(void)
{

    solMark = Time.time;
}

void earth_update(void)
{
    // Step 5 of the plan ports the rest of Earth.Update before this point,
    // including the decode check that reads solMark.
    (void)solMark;

    // Deliberate change (plan 6.4): Enter also dismisses. Gamepad A and the
    // touch Pull button send Space.
    if (input_get_key_down(KEY_SPACE) || input_get_key_down(KEY_ENTER)) {
        focus.newMessage.enabled = false;
        focus.couldDecode.enabled = false;
        focus.newMessage.enabled = false;
        focus.tutorial.enabled = false;
        Time.timeScale = EARTH_RESUME_TIME_SCALE; // original: // resume
        audio_listener_set_volume(EARTH_RESUME_AUDIO_VOLUME);
    }

    missionUpdateIndicator.enabled = willLoadNewLevel;
    // roverNewMessageSound.volume comes with the Rover's sound, step 5.
    // original: // animate
    Rgba c = missionUpdateIndicator.color;
    c.a += Time.deltaTime * EARTH_INDICATOR_FADE_RATE;
    if (c.a >= EARTH_INDICATOR_ALPHA_WRAP) c.a = EARTH_INDICATOR_ALPHA_AFTER_WRAP;
    missionUpdateIndicator.color = c;

}

// Kept bug (plan 6.3, item 8): the parameter has the name of the
// newMessage field, so the field is never hidden here.
static void showCutscene(SpriteRenderer *newMessage)
{

    // The repeated assignments are as in the source.
    newMessage->enabled = false;
    focus.couldDecode.enabled = false;
    // cppcheck-suppress redundantAssignment
    newMessage->enabled = false;
    focus.tutorial.enabled = false;


    // cppcheck-suppress redundantAssignment
    newMessage->enabled = true;
    Time.timeScale = EARTH_PAUSE_TIME_SCALE; // original: // pause
    audio_listener_set_volume(EARTH_PAUSE_AUDIO_VOLUME);
    rover_screen_set_on(false);
}

// willLoadNewLevel stays false until step 5 brings the messages, so this
// does nothing yet.
void earth_player_is_in_rover(void)
{

    // cppcheck-suppress duplicateConditionalAssign
    if (willLoadNewLevel) { // original: // test level progression by uncommenting this
        willLoadNewLevel = false;
    }

}
