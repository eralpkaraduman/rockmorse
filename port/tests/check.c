// Logic checks that need no window. Run from the port folder, so the font
// in assets/ is found:
//   ./build/rockmorse_check
#include "../src/scenes.h"
#include "../src/code_constants.h"

// Release builds define NDEBUG, which would remove every assert below,
// including the setup calls written inside them. Keep them in all builds.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

// --- GetAxis smoothing -------------------------------------------------------

static void check_axis(void)
{
    const float dt = UNITY_FRAME_TIME;
    const float s = INPUT_KEY_HORIZONTAL_SENSITIVITY, g = INPUT_KEY_HORIZONTAL_GRAVITY;
    assert(s == 3 && g == 3 && INPUT_KEY_HORIZONTAL_SNAP == 1 && INPUT_KEY_HORIZONTAL_DEAD == 0.001f);

    // Holding right rises by sensitivity per second: 0.05 per 60 Hz frame,
    // full after a third of a second.
    float v = 0;
    v = axis_smooth(v, true, false, dt, s, g, true);
    assert(near(v, 0.05f, 1e-6f));
    int frames = 1;
    while (v < 1) {
        v = axis_smooth(v, true, false, dt, s, g, true);
        frames++;
    }
    assert(v == 1 && frames >= 20 && frames <= 21);
    printf("axis: right held reaches 1 after %d frames (%.3f s)\n", frames, frames * dt);

    // Letting go falls by gravity per second.
    v = axis_smooth(1, false, false, dt, s, g, true);
    assert(near(v, 0.95f, 1e-6f));

    // Snap: pressing the opposite way jumps to 0 first, then moves.
    v = axis_smooth(0.5f, false, true, dt, s, g, true);
    assert(near(v, -0.05f, 1e-6f));
    // Without snap it only moves by sensitivity.
    v = axis_smooth(0.5f, false, true, dt, s, g, false);
    assert(near(v, 0.45f, 1e-6f));
    // Both keys held count as none.
    v = axis_smooth(0.5f, true, true, dt, s, g, true);
    assert(near(v, 0.45f, 1e-6f));
    // The top speed of the player is 0.8 * 1 * 7.2 = 5.76 units per second.
    assert(near(PLAYER_VELOCITY_LERP_T * 1 * (PLAYER_SPEED + PLAYER_AGILITY / PLAYER_AGILITY_DIVISOR) *
                PLAYER_WALK_SPEED_FACTOR, 5.76f, 1e-5f));

    // Dead zones.
    assert(axis_dead_zone(0.0005f, INPUT_KEY_HORIZONTAL_DEAD) == 0);
    assert(axis_dead_zone(0.05f, INPUT_KEY_HORIZONTAL_DEAD) == 0.05f);
    assert(axis_dead_zone(-0.18f, INPUT_STICK_HORIZONTAL_DEAD) == 0);
    assert(axis_dead_zone(-0.2f, INPUT_STICK_HORIZONTAL_DEAD) == -0.2f);
}

// --- Mathf ------------------------------------------------------------------

static void check_round(void)
{
    // C#'s Math.Round rounds halves to even.
    assert(mathf_round(0.5f) == 0);
    assert(mathf_round(1.5f) == 2);
    assert(mathf_round(2.5f) == 2);
    assert(mathf_round(3.5f) == 4);
    assert(mathf_round(-0.5f) == 0);
    assert(mathf_round(-1.5f) == -2);
    assert(mathf_round(2.4999998f) == 2);
    assert(mathf_round(99.5f) == 100);
    assert(mathf_round(66.5f) == 66);
    printf("round: 0.5 1.5 2.5 3.5 -> %g %g %g %g\n", mathf_round(0.5f), mathf_round(1.5f),
           mathf_round(2.5f), mathf_round(3.5f));
}

// The decompiled SmoothDamp, worked through in double precision with the
// steps written differently, to catch a mistyped operator or order.
static double smooth_damp_reference(double current, double target, double velocity, double smoothTime, double dt)
{
    double omega = 2 / smoothTime;
    double x = omega * dt;
    double exp = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x);
    double change = current - target; // maxSpeed is infinite, so no clamp
    double temp = (velocity + omega * change) * dt;
    double result = target + (change + temp) * exp;
    if ((target - current > 0) == (result > target)) result = target;
    return result;
}

static void check_smooth_damp(void)
{
    // iTween.MoveUpdate: smooth time is the speed argument times 0.05, and
    // the velocity starts at 0 on every call.
    const float follow = FOCUS_SWITCHER_SPEED_PLAYER * ITWEEN_UPDATE_TIME_PERCENTAGE;   // 0.01 s
    const float overview = FOCUS_SWITCHER_SPEED_OVERVIEW * ITWEEN_UPDATE_TIME_PERCENTAGE; // 0.05 s
    const float dt = UNITY_FRAME_TIME;
    const float cases[][3] = {
        {0, 10, follow}, {0, 10, overview}, {-1000, 6.56f, follow}, {-1000, 6.56f, overview},
        {5, -3, overview}, {4.76f, 4.5f, follow}, {0, 1e-3f, overview},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        float v = 0;
        float got = mathf_smooth_damp(cases[i][0], cases[i][1], &v, cases[i][2], INFINITY, dt);
        double want = smooth_damp_reference(cases[i][0], cases[i][1], 0, cases[i][2], dt);
        assert(fabs(got - want) <= 1e-4 * (1 + fabs(want)));
    }
    float v = 0;
    float step = mathf_smooth_damp(0, 10, &v, follow, INFINITY, dt);
    printf("smooth damp: 0 -> 10, smooth time 0.01, one 60 Hz frame: %.6f\n", step);
    assert(near(step, 7.641f, 1e-3f));

    // At deltaTime 0 (game paused) the camera stays where it is.
    v = 0;
    assert(mathf_smooth_damp(-1000, 6.56f, &v, follow, INFINITY, 0) == -1000);
    // A step that would overshoot, here from an incoming velocity, lands on
    // the target exactly.
    v = 100;
    assert(mathf_smooth_damp(9.999f, 10, &v, overview, INFINITY, 1) == 10 && v == 0);
    // Already on target: the last branch divides 0 by deltaTime 0, so the
    // velocity is NaN. The game drops it.
    v = 0;
    assert(mathf_smooth_damp(3, 3, &v, follow, INFINITY, 0) == 3 && isnan(v));
}

// --- Frame loop and time ----------------------------------------------------

static int starts[SCENE_COUNT], fixedUpdates, updates, lateUpdates;
static float fixedTimeSeen, fixedDeltaSeen;
static SceneId loadDuringUpdate = SCENE_COUNT;

static void counting_start(void) { starts[scene_active()]++; }
static void counting_fixed(void) { fixedUpdates++; fixedTimeSeen = Time.time; fixedDeltaSeen = Time.deltaTime; }
static void counting_update(void)
{
    updates++;
    if (loadDuringUpdate != SCENE_COUNT) {
        scene_load(loadDuringUpdate);
        loadDuringUpdate = SCENE_COUNT;
    }
}
static void counting_late(void) { lateUpdates++; }

static const Scene COUNTING_A = {.name = "A", .start = counting_start, .fixed_update = counting_fixed,
                                 .update = counting_update, .late_update = counting_late};
static const Scene COUNTING_B = {.name = "B", .start = counting_start, .fixed_update = counting_fixed,
                                 .update = counting_update, .late_update = counting_late};

static void check_time(void)
{
    const Scene *table[SCENE_COUNT] = {&COUNTING_A, &COUNTING_B, &COUNTING_A, &COUNTING_A,
                                       &COUNTING_A, &COUNTING_A, &COUNTING_A};
    unity_init(table, SCENE_INTRO);
    assert(Time.timeScale == TIME_SCALE_START && Time.fixedDeltaTime == FIXED_TIMESTEP);

    // One second at 60 Hz: 60 updates and 50 fixed updates.
    for (int i = 0; i < 60; i++) unity_frame();
    assert(starts[SCENE_INTRO] == 1 && updates == 60 && lateUpdates == 60);
    assert(fixedUpdates == 50);
    assert(near(Time.time, 1, 1e-5f) && near(Time.deltaTime, UNITY_FRAME_TIME, 1e-7f));
    // Inside FixedUpdate, time is the fixed time and deltaTime the fixed step.
    assert(fixedDeltaSeen == FIXED_TIMESTEP && near(fixedTimeSeen, 1, 1e-5f));

    // timeScale 0: time and deltaTime stop, Update runs, FixedUpdate does not.
    Time.timeScale = 0;
    float frozen = Time.time;
    for (int i = 0; i < 30; i++) unity_frame();
    assert(Time.time == frozen && Time.deltaTime == 0);
    assert(updates == 90 && fixedUpdates == 50);
    assert(Time.unscaledDeltaTime == UNITY_FRAME_TIME);

    // A scene change waits for the end of the frame: LateUpdate of the old
    // scene still runs, and the new scene starts in the next frame. Time,
    // timeScale and the listener volume carry over.
    audio_listener_set_volume(0);
    loadDuringUpdate = SCENE_TITLE;
    unity_frame();
    assert(scene_active() == SCENE_INTRO && lateUpdates == 91 && starts[SCENE_TITLE] == 0);
    unity_frame();
    assert(scene_active() == SCENE_TITLE && starts[SCENE_TITLE] == 1);
    assert(Time.time == frozen && Time.timeScale == 0 && audio_listener_volume() == 0);

    // Half speed: game time grows by half a frame per frame.
    Time.timeScale = 0.5f;
    unity_frame();
    assert(near(Time.time - frozen, UNITY_FRAME_TIME / 2, 1e-6f));
    Time.timeScale = 1;
    audio_listener_set_volume(1);
    printf("time: 60 frames give %d fixed updates; at timeScale 0 time stays %.4f\n", 50, frozen);
}

// --- The real scenes --------------------------------------------------------

static void game_stub_start(void) {}
static const Scene GAME_STUB = {.name = "Game", .start = game_stub_start};

static void check_scenes(void)
{
    const Scene *table[SCENE_COUNT] = {&INTRO_SCENE, &TITLE_SCENE, &STORY1_SCENE, &STORY2_SCENE,
                                       &GAME_STUB, &DEATH_SCENE, &WIN_SCENE};
    unity_init(table, SCENE_INTRO);

    // Build order, and every scene name the scripts load by name.
    const char *names[SCENE_COUNT] = {"Intro", "Title", "Story1", "Story2", "Game", "Death", "Win"};
    for (int i = 0; i < SCENE_COUNT; i++) {
        assert(scene_find(names[i]) == i && strcmp(scene_name((SceneId)i), names[i]) == 0);
    }
    assert(scene_find(INTRO_NEXT_SCENE) == SCENE_TITLE);
    assert(scene_find(TITLE_PLAY_NEXT_SCENE) == SCENE_STORY1);
    assert(scene_find(STORY1_NEXT_SCENE) == SCENE_STORY2);
    assert(scene_find(STORY2_NEXT_SCENE) == SCENE_GAME);
    assert(scene_find(ROVER_SCREEN_DEATH_SCENE) == SCENE_DEATH);
    assert(scene_find(ROCKET_WIN_SCENE) == SCENE_WIN);
    assert(END_SCREEN_NEXT_SCENE_INDEX == SCENE_INTRO);
    assert(scene_find("Nowhere") == -1);

    // The intro lasts 2 s: 120 frames, then Title starts in the next one.
    int frames = 0;
    while (scene_active() != SCENE_TITLE && frames < 1000) {
        unity_frame();
        frames++;
    }
    printf("scenes: Title starts at frame %d (intro 2 s at 60 Hz)\n", frames);
    assert(frames >= 121 && frames <= 122);

    // Story1 waits 20 s, then Story2 loads.
    scene_load(SCENE_STORY1);
    unity_frame();
    assert(scene_active() == SCENE_STORY1);
    frames = 0;
    while (scene_active() == SCENE_STORY1 && frames < 5000) {
        unity_frame();
        frames++;
    }
    assert(scene_active() == SCENE_STORY2 && frames >= 1200 && frames <= 1202);
    printf("scenes: Story1 moves on by itself after %d frames\n", frames);
}

// --- Mouse and GUIText -----------------------------------------------------

static void check_mouse(void)
{
    // OnMouseUp goes to the button that got the mouse down, wherever the
    // mouse is released.
    MouseTarget t = {0};
    MouseEvents e = mouse_target_update(&t, true, false, false);
    assert(e.enter && !e.exit && !e.up);
    e = mouse_target_update(&t, true, true, false); // down on the button
    assert(!e.enter && !e.up);
    e = mouse_target_update(&t, false, false, false); // drag off it
    assert(e.exit && !e.up);
    e = mouse_target_update(&t, false, false, true); // release elsewhere
    assert(e.up);
    // Down elsewhere, released over the button: no click.
    t = (MouseTarget){0};
    (void)mouse_target_update(&t, false, true, false);
    e = mouse_target_update(&t, true, false, false);
    assert(e.enter);
    e = mouse_target_update(&t, true, false, true);
    assert(!e.up);
    // A plain click over the button.
    e = mouse_target_update(&t, true, true, true);
    assert(e.up);

    // Hit rectangles measured on the 2016 WebGL build by hovering PLAY and
    // SUPER DAMAGE pixel by pixel: columns 248 to 411 and rows 322 to 380,
    // columns 246 to 413 and rows 219 to 238.
    GUIText play = GUI_TEXT(TITLE_PLAY), sd = GUI_TEXT(TITLE_SD);
    Rectangle r = gui_text_screen_rect(&play);
    printf("gui text: PLAY hit box x %g y %g w %g h %g\n", r.x, r.y, r.width, r.height);
    assert(r.x == 248 && r.y == 322 && r.width == 164 && r.height == 59);
    r = gui_text_screen_rect(&sd);
    printf("gui text: SUPER DAMAGE hit box x %g y %g w %g h %g\n", r.x, r.y, r.width, r.height);
    assert(r.x == 246 && r.y == 219 && r.width == 168 && r.height == 20);
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    check_axis();
    check_round();
    check_smooth_damp();
    check_time();
    check_scenes();
    check_mouse();
    printf("All checks passed.\n");
    return 0;
}
