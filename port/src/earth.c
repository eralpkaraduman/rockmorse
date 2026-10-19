// Earth.cs, on the Game scene's Earth object: the message flow. In daylight
// Earth reads the rocks. If the answer is right, the map shows it for 2 s,
// then a "decoded" screen. After the telemetry delay a new message waits in
// the rover. Entering the rover loads it, and after the last one the rocket
// comes down. It also shows the tutorial and the update indicator.
#include "game.h"

AudioSource roverNewMessageSound;

// Earth.cs private fields.
static float didDecodeMark;
static bool earthWillRespond;
static float telemetryDelay;
static float maxMessageSoundVolume;
static bool willLoadNewLevel; // original: // should enter rover, wont decode as message is old
static bool lastLevel;
static float solMark;
static bool displayingDecodedMessage;
static float timeMarkBeganDisplayingDecodedMessage;
static float decodedMessageDisplayDuration;
static int debugForceNextLevel;
// (testIgnoreRoverEnterance and the rocket field are never read.)

// original: //private float tryToDecodeTime = Mathf.Infinity;

static void showCutscene(SpriteRenderer *newMessage);
static void decode(void);
static void endDisplayingDecodedMessage(void);
static void onNewMessage(void);

// original: // Use this for initialization
void earth_start(void)
{
    didDecodeMark = INFINITY;
    earthWillRespond = false;
    telemetryDelay = EARTH_TELEMETRY_DELAY;
    maxMessageSoundVolume = EARTH_MAX_MESSAGE_SOUND_VOLUME;
    willLoadNewLevel = false;
    lastLevel = false;
    solMark = INFINITY;
    displayingDecodedMessage = false;
    timeMarkBeganDisplayingDecodedMessage = 0;
    decodedMessageDisplayDuration = EARTH_DECODED_MESSAGE_DISPLAY_DURATION;
    debugForceNextLevel = EARTH_DEBUG_FORCE_NEXT_LEVEL;

    Rgba c = missionUpdateIndicator.color;
    c.a = EARTH_INDICATOR_START_ALPHA;
    missionUpdateIndicator.color = c;

    // Kept as in the source (plan 6.3, item 8): showCutscene mutes all
    // sound, but the next line turns it back up, so the first tutorial
    // plays with sound.
    showCutscene(&focus.tutorial);
    audio_listener_set_volume(EARTH_START_AUDIO_VOLUME);
}

void earth_new_sol(void)
{

    solMark = Time.time;
}

void earth_update(void)
{

    float t = Time.time;

    bool forceDismissCutscene = false;
    // Deliberate changes (plan 6.4). Shift also toggles the tutorial, and
    // the gamepad's Start button sends H. The tutorial keys are ignored
    // while the 2 s decoded overview shows: in the original, a press then
    // paused time, so the overview never ended, and the return below
    // skipped the Space check. The game froze for good.
    bool overviewShowing = displayingDecodedMessage && timeMarkBeganDisplayingDecodedMessage > 0;
    bool tutorialKey = input_get_key_down(KEY_H) || input_get_key_down(KEY_ESCAPE) || input_get_key_down(KEY_P) ||
                       input_get_key_down(KEY_LEFT_SHIFT) || input_get_key_down(KEY_RIGHT_SHIFT);
    if (tutorialKey && !overviewShowing) {
        if (focus.tutorial.enabled == false) {
            showCutscene(&focus.tutorial);
        } else {
            forceDismissCutscene = true;
        }

    }


    // displayingDecodedMessage never goes back to false, because
    // continueAfterDecodedDisplay has no caller. After the first overview
    // the mark is 0, so this passes every frame, and the end function
    // returns at once. Kept as in the source.
    if (displayingDecodedMessage) {
        if ((timeMarkBeganDisplayingDecodedMessage + decodedMessageDisplayDuration) < t) {
            endDisplayingDecodedMessage();
        } else {
            return;
        }

    }

    // original: // can decode
    if (t > solMark + telemetryDelay && mars.isEarthVisible && willLoadNewLevel == false && earthWillRespond == false) {
        decode();
    } else {
        // original: //Debug.Log("not decoding");
    }

    if (t > (didDecodeMark + telemetryDelay) && willLoadNewLevel == false) {
        didDecodeMark = INFINITY;
        onNewMessage();
    }

    // Deliberate change (plan 6.4): Enter also dismisses. Gamepad A and the
    // touch Pull button send Space.
    if (input_get_key_down(KEY_SPACE) || input_get_key_down(KEY_ENTER) || forceDismissCutscene) {
        focus.newMessage.enabled = false;
        focus.couldDecode.enabled = false;
        // cppcheck-suppress redundantAssignment
        focus.newMessage.enabled = false; // twice, as in the source
        focus.tutorial.enabled = false;
        Time.timeScale = EARTH_RESUME_TIME_SCALE; // original: // resume
        audio_listener_set_volume(EARTH_RESUME_AUDIO_VOLUME);
    }

    missionUpdateIndicator.enabled = willLoadNewLevel;
    // A 3D sound at the rover in the original. Its distance fade is step 7
    // of the plan.
    roverNewMessageSound.volume = willLoadNewLevel ? maxMessageSoundVolume : EARTH_MESSAGE_SOUND_VOLUME_IDLE;
    // original: // animate
    Rgba c = missionUpdateIndicator.color;
    c.a += Time.deltaTime * EARTH_INDICATOR_FADE_RATE;
    if (c.a >= EARTH_INDICATOR_ALPHA_WRAP) c.a = EARTH_INDICATOR_ALPHA_AFTER_WRAP;
    missionUpdateIndicator.color = c;

}

static void beginDisplayingDecodedMessage(void)
{
    displayingDecodedMessage = true;
    timeMarkBeganDisplayingDecodedMessage = Time.time;
    camera_focus_force_overview(true);
}

static void decode(void)
{

    bool decoded = level_loader_check();

    if (levelLoader.currentLevel < 0) { // original: // first level
        earthWillRespond = true;
        didDecodeMark = Time.time;
        // original: //onNewMessage();

    // cppcheck-suppress knownConditionTrueFalse
    } else if (decoded && levelLoader.currentLevel < 0) { // original: // first level
        // Never reached: the branch above takes currentLevel < 0.
        earthWillRespond = true;
        didDecodeMark = Time.time;
        // original: //onNewMessage();

    } else if (decoded == true) {

        didDecodeMark = Time.time;
        earthWillRespond = true;
        beginDisplayingDecodedMessage();

    } // original: /*else if(decoded == false){
    // original: 			showCutscene(couldNotDecode);
    // original: 		}*/
}

static void endDisplayingDecodedMessage(void)
{

    if (timeMarkBeganDisplayingDecodedMessage <= 0) return;


    timeMarkBeganDisplayingDecodedMessage = 0;
    camera_focus_force_overview(false);

    didDecodeMark = Time.time;

    showCutscene(&focus.couldDecode);

}

// continueAfterDecodedDisplay() sets displayingDecodedMessage = false. It
// has no caller, so the port leaves it out.

static void onNewMessage(void)
{
    // original: //newMessage.enabled = true;
    showCutscene(&focus.newMessage);

    willLoadNewLevel = true;
    earthWillRespond = false;
}

// Kept bug (plan 6.3, item 8): the parameter has the name of the
// newMessage field, so the field is never hidden here. Showing the
// tutorial over a new message leaves both on; draw order picks the top one.
// couldNotDecode is never hidden either.
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

static void beginLastLevel(void)
{
    rocket_spawn(); // Instantiate(rocketPrefab)
}

void earth_player_is_in_rover(void)
{

    if (willLoadNewLevel) { // original: // test level progression by uncommenting this

        bool notLastLevel = level_loader_next_level(debugForceNextLevel);
        debugForceNextLevel = EARTH_FORCE_NEXT_LEVEL_NONE;

        lastLevel = !notLastLevel;
        if (lastLevel) beginLastLevel();

        // original: //decoding = true;
        // original: //Debug.Log("set true2");
        willLoadNewLevel = false;


    }

}
