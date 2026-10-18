// Death and Win scenes and their script, EndGame.cs. Each shows one full
// screen image with its sounds, then goes back to the intro (build index
// 0) after 5 seconds, on Space or on a left click.
#include "scenes.h"
#include "code_constants.h"

static UnityCamera camera;
static GUITexture gfx;
static AudioSource sounds[2];
static int soundCount;

// EndGame.cs fields.
static const float dismissDuration = END_SCREEN_DURATION;
static float timeSceneEntered = 0;

// original: // Use this for initialization
static void Start(void)
{
    timeSceneEntered = Time.time;
}

static void dismiss(void)
{
    scene_load(END_SCREEN_NEXT_SCENE_INDEX);
}

// original: // Update is called once per frame
static void update(void)
{
    if (Time.time >= (timeSceneEntered + dismissDuration)) {
        dismiss();
    }

    // Deliberate change (plan 6.4): Enter also goes on.
    if (input_get_key_down(KEY_SPACE) || input_get_mouse_button_down(END_SCREEN_MOUSE_BUTTON) ||
        input_get_key_down(KEY_ENTER)) {
        dismiss();
    }
}

static void draw(void)
{
    camera_clear(&camera);
    gui_texture_draw(&gfx);
}

static void death_start(void)
{
    camera = CAMERA(DEATH_CAMERA);
    gfx = GUI_TEXTURE(DEATH_IMAGE);
    sounds[0] = AUDIO_SOURCE(DEATH_SOUND);
    soundCount = 1;
    audio_source_awake(&sounds[0]);
    Start();
}

static void win_start(void)
{
    camera = CAMERA(WIN_CAMERA);
    gfx = GUI_TEXTURE(WIN_IMAGE);
    sounds[0] = AUDIO_SOURCE(WIN_MUSIC);
    sounds[1] = AUDIO_SOURCE(WIN_HORN);
    soundCount = 2;
    for (int i = 0; i < soundCount; i++) audio_source_awake(&sounds[i]);
    Start();
}

const Scene DEATH_SCENE = {.name = "Death", .start = death_start, .update = update, .draw = draw};
const Scene WIN_SCENE = {.name = "Win", .start = win_start, .update = update, .draw = draw};
