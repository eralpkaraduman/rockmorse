// Story1 and Story2 scenes and their script, NextSceneOnSpace.cs. Each
// shows one full screen story image and plays Menu4 on awake. Space or a
// left click goes on, and so does waiting 20 seconds.
#include "scenes.h"
#include "code_constants.h"

static UnityCamera camera;
static GUITexture gfx;
static AudioSource menu4;

// NextSceneOnSpace.cs fields.
static float dismissDuration;
static float timeSceneEntered = 0;
static const char *nextScene;

// original: // Use this for initialization
static void Start(void)
{
    timeSceneEntered = Time.time;
}

static void dismiss(void)
{
    scene_load_name(nextScene);
}

static void update(void)
{
    if (Time.time >= (timeSceneEntered + dismissDuration)) {
        dismiss();
    }

    // Deliberate change (plan 6.4): Enter also goes on.
    if (input_get_key_down(KEY_SPACE) || input_get_mouse_button_down(NEXT_SCENE_MOUSE_BUTTON) ||
        input_get_key_down(KEY_ENTER)) {
        dismiss();
    }
}

static void draw(void)
{
    camera_clear(&camera);
    gui_texture_draw(&gfx);
}

static void story1_start(void)
{
    camera = CAMERA(STORY1_CAMERA);
    gfx = GUI_TEXTURE(STORY1_IMAGE);
    menu4 = AUDIO_SOURCE(STORY1_SOUND);
    dismissDuration = STORY1_DISMISS_DURATION;
    nextScene = STORY1_NEXT_SCENE;
    audio_source_awake(&menu4);
    Start();
}

static void story2_start(void)
{
    camera = CAMERA(STORY2_CAMERA);
    gfx = GUI_TEXTURE(STORY2_IMAGE);
    menu4 = AUDIO_SOURCE(STORY2_SOUND);
    dismissDuration = STORY2_DISMISS_DURATION;
    nextScene = STORY2_NEXT_SCENE;
    audio_source_awake(&menu4);
    Start();
}

const Scene STORY1_SCENE = {.name = "Story1", .start = story1_start, .update = update, .draw = draw};
const Scene STORY2_SCENE = {.name = "Story2", .start = story2_start, .update = update, .draw = draw};
