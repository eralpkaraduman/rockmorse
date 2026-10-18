// Intro scene and its script, Intro.cs. The Super Damage logo appears after
// a tenth of a second, then the title loads after 2 seconds. The explosion
// sound plays on awake. The 2016 build showed Unity's splash screen before
// this scene; the port leaves it out, as it is not part of the game.
#include "scenes.h"
#include "code_constants.h"

static UnityCamera camera;
static SpriteRenderer logo;
static AudioSource explosion;

// Intro.cs fields.
static const float dur = INTRO_DURATION;
static float introDuration;

static void start(void)
{
    camera = CAMERA(INTRO_CAMERA);
    logo = (SpriteRenderer){
        .transform = {.position = {INTRO_LOGO_POS_X, INTRO_LOGO_POS_Y, INTRO_LOGO_POS_Z},
                      .scale = {INTRO_LOGO_SCALE_X, INTRO_LOGO_SCALE_Y, INTRO_LOGO_SCALE_Z}},
        .sprite = INTRO_LOGO_SPRITE,
        .color = {INTRO_LOGO_COLOR_R, INTRO_LOGO_COLOR_G, INTRO_LOGO_COLOR_B, INTRO_LOGO_COLOR_A},
        .sortingOrder = INTRO_LOGO_SORTING_ORDER,
        .enabled = INTRO_LOGO_ENABLED,
    };
    explosion = AUDIO_SOURCE(INTRO_SOUND);
    audio_source_awake(&explosion);

    // Intro.Start
    introDuration = dur;
    logo.enabled = false;
}

static void update(void)
{
    introDuration -= Time.deltaTime;
    // 0.1 is a double in C#, so the test runs in double precision.
    if ((double)introDuration <= (double)dur - INTRO_LOGO_DELAY_DOUBLE) {
        logo.enabled = true;
    }

    if (introDuration <= 0) {
        scene_load_name(INTRO_NEXT_SCENE);
    }
}

static void draw(void)
{
    camera_clear(&camera);
    SpriteRenderer *sprites[] = {&logo};
    draw_sprites(&camera, sprites, 1);
}

const Scene INTRO_SCENE = {.name = "Intro", .start = start, .update = update, .draw = draw};
