// Logic checks that need no window. Run from the port folder, so the font
// in assets/ is found:
//   ./build/rockmorse_check
#include "../src/scenes.h"
#include "../src/game.h"
#include "../src/code_constants.h"
#include "../src/tables.h"

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

// --- Convex pieces ------------------------------------------------------------

// Box2D drops a corner flatter than its slop, and a release build swaps a
// bad hull for a small box without a word. So every piece must keep all its
// points, with the game's length unit set first (plan section 6.1).
static void check_hulls(void)
{
    b2SetLengthUnitsPerMeter(LENGTH_UNITS_PER_METER);
    assert(near(B2_LINEAR_SLOP_USED, 0.005f * LENGTH_UNITS_PER_METER, 1e-9f));
    struct { const char *name; const float *points; const int *sizes; int count; } sets[] = {
        {"player body", PLAYER_BODY_PIECES_POINTS, PLAYER_BODY_PIECES_SIZES, PLAYER_BODY_PIECES_COUNT},
        {"rover roof", ROVER_ROOF_COLLIDER_PIECES_POINTS, ROVER_ROOF_COLLIDER_PIECES_SIZES, ROVER_ROOF_COLLIDER_PIECES_COUNT},
        {"rocket body", ROCKET_BODY_COLLIDER_PIECES_POINTS, ROCKET_BODY_COLLIDER_PIECES_SIZES, ROCKET_BODY_COLLIDER_PIECES_COUNT},
    };
    for (size_t k = 0; k < sizeof sets / sizeof sets[0]; k++) {
        const float *p = sets[k].points;
        for (int i = 0; i < sets[k].count; i++) {
            int n = sets[k].sizes[i];
            assert(n >= 3 && n <= B2_MAX_POLYGON_VERTICES);
            b2Vec2 v[B2_MAX_POLYGON_VERTICES];
            for (int j = 0; j < n; j++) v[j] = (b2Vec2){p[2 * j], p[2 * j + 1]};
            b2Hull hull = b2ComputeHull(v, n);
            assert(hull.count == n && b2ValidateHull(&hull));
            // The mirrored piece, used while the player faces left, too.
            for (int j = 0; j < n; j++) v[j].x = -v[j].x;
            hull = b2ComputeHull(v, n);
            assert(hull.count == n);
            p += 2 * n;
        }
        printf("hulls: %s, %d pieces keep all their points\n", sets[k].name, sets[k].count);
    }
}

// --- Physics bridge -------------------------------------------------------------

#define MAX_LOG 64
typedef struct { PhysicsEvent e; int self, other; bool otherRockAlive; } LogEntry;
static LogEntry eventLog[MAX_LOG];
static int logCount;
static RockRef watchedRock;

static void record(PhysicsEvent e, const Collider *self, const Collider *other)
{
    assert(logCount < MAX_LOG);
    eventLog[logCount++] = (LogEntry){e, self->kind, other->kind, rock_get(watchedRock) != NULL};
}

static int count_events(PhysicsEvent e, int self, int other)
{
    int n = 0;
    for (int i = 0; i < logCount; i++) n += eventLog[i].e == e && eventLog[i].self == self && eventLog[i].other == other;
    return n;
}

static void steps(int n)
{
    for (int i = 0; i < n; i++) physics_step();
}

enum { K_SENSOR_BOX = 100, K_POLY, K_SENSOR_A, K_SENSOR_B, K_ROOF, K_REPELLER, K_BALL, K_ZONE };

static void check_physics_bridge(void)
{
    physics_init(record);

    // A multi-piece solid polygon (the player's 3 pieces) walks through a
    // static trigger box. Each owner gets one enter and one exit, though
    // up to 3 Box2D shape pairs touch.
    logCount = 0;
    b2BodyId box = physics_body(b2_staticBody, (b2Vec2){0, 0}, 0, 0, 0, false);
    physics_box(box, (Collider){K_SENSOR_BOX, 0, TAG_UNTAGGED, true}, (b2Vec2){0, 0}, (b2Vec2){2, 2});
    b2BodyId walker = physics_body(b2_dynamicBody, (b2Vec2){-3, 0}, 0, 0, 0, true);
    physics_polygon(walker, (Collider){K_POLY, 0, PLAYER_TAG, false}, PLAYER_BODY_PIECES_POINTS,
                    PLAYER_BODY_PIECES_SIZES, PLAYER_BODY_PIECES_COUNT);
    physics_set_mass(walker, PLAYER_RB_MASS);
    assert(near(b2Body_GetMass(walker), PLAYER_RB_MASS, 1e-6f));
    for (int i = 0; i < 300; i++) {
        b2Body_SetLinearVelocity(walker, (b2Vec2){1, 0}); // 6 units in 6 s
        physics_step();
    }
    assert(b2Body_GetPosition(walker).x > 2.5f);
    assert(count_events(TRIGGER_ENTER, K_SENSOR_BOX, K_POLY) == 1 && count_events(TRIGGER_ENTER, K_POLY, K_SENSOR_BOX) == 1);
    assert(count_events(TRIGGER_EXIT, K_SENSOR_BOX, K_POLY) == 1 && count_events(TRIGGER_EXIT, K_POLY, K_SENSOR_BOX) == 1);
    assert(logCount == 4);
    printf("physics: a 3 piece polygon through a trigger gives 1 enter and 1 exit to each owner\n");

    // Two sensors that overlap: Box2D reports the pair from both sides,
    // the bridge once. Trigger against trigger fires, as plan 6.3 item 1
    // needs for the player's circle and the rover's inside trigger.
    logCount = 0;
    b2BodyId a = physics_body(b2_dynamicBody, (b2Vec2){10, 0}, 0, 0, 0, true);
    physics_circle(a, (Collider){K_SENSOR_A, 0, PLAYER_TAG, true}, (b2Vec2){0, 0}, 1);
    physics_set_mass(a, 1);
    b2BodyId b = physics_body(b2_staticBody, (b2Vec2){11.5f, 0}, 0, 0, 0, false);
    physics_circle(b, (Collider){K_SENSOR_B, 0, TAG_UNTAGGED, true}, (b2Vec2){0, 0}, 0.6f);
    steps(2);
    assert(count_events(TRIGGER_ENTER, K_SENSOR_A, K_SENSOR_B) == 1 && count_events(TRIGGER_ENTER, K_SENSOR_B, K_SENSOR_A) == 1);
    assert(logCount == 2);
    b2Body_SetTransform(a, (b2Vec2){20, 0}, b2Rot_identity);
    steps(2);
    assert(count_events(TRIGGER_EXIT, K_SENSOR_A, K_SENSOR_B) == 1 && count_events(TRIGGER_EXIT, K_SENSOR_B, K_SENSOR_A) == 1);
    assert(logCount == 4);
    printf("physics: two overlapping triggers give one enter per owner, not two\n");

    // A solid ball on the rover's roof polygon and in a trigger at once
    // gets both a collision and a trigger enter, counted apart.
    logCount = 0;
    b2BodyId roof = physics_body(b2_kinematicBody, (b2Vec2){30, 0}, 0, 0, 0, false);
    physics_polygon(roof, (Collider){K_ROOF, 0, ROVER_ROOF_TAG, false}, ROVER_ROOF_COLLIDER_PIECES_POINTS,
                    ROVER_ROOF_COLLIDER_PIECES_SIZES, ROVER_ROOF_COLLIDER_PIECES_COUNT);
    b2BodyId rep = physics_body(b2_staticBody, (b2Vec2){30, 0}, 0, 0, 0, false);
    physics_circle(rep, (Collider){K_REPELLER, 0, ROCK_REPELLER_TAG, true}, (b2Vec2){0, 0}, 3.29f);
    b2BodyId ball = physics_body(b2_dynamicBody, (b2Vec2){30 + 2.0f, 0}, 0, 0, 0, false);
    physics_circle(ball, (Collider){K_BALL, 0, TAG_UNTAGGED, false}, (b2Vec2){0, 0}, ROCK_RADIUS_WORLD);
    physics_set_mass(ball, ROCK_RB_MASS);
    for (int i = 0; i < 60; i++) {
        b2Body_SetLinearVelocity(ball, (b2Vec2){-1, 0});
        physics_step();
    }
    assert(count_events(TRIGGER_ENTER, K_BALL, K_REPELLER) == 1 && count_events(TRIGGER_ENTER, K_REPELLER, K_BALL) == 1);
    assert(count_events(COLLISION_ENTER, K_BALL, K_ROOF) == 1 && count_events(COLLISION_ENTER, K_ROOF, K_BALL) == 1);
    // The roof (kinematic) and the repeller (static) overlap, but neither is
    // dynamic, so Unity's Box2D 2.x reports nothing for them.
    assert(count_events(TRIGGER_ENTER, K_ROOF, K_REPELLER) == 0);
    printf("physics: a ball on the roof and in the repeller gets a collision and a trigger\n");

    // Mass: set after the shapes, inertia scaled by the same ratio.
    b2MassData m = b2Body_GetMassData(ball);
    float r = ROCK_RADIUS_WORLD;
    assert(near(m.mass, ROCK_RB_MASS, 1e-4f) && near(m.rotationalInertia, 0.5f * ROCK_RB_MASS * r * r, 1e-4f));
    // Damping: 4 Box2D sub-steps give Unity's one-step factor 1 / (1 + h c).
    float c = physics_damping(ROCK_RB_LINEAR_DRAG), h = FIXED_TIMESTEP / PHYSICS_SUB_STEPS;
    assert(near(powf(1 / (1 + h * c), PHYSICS_SUB_STEPS), 1 / (1 + FIXED_TIMESTEP * ROCK_RB_LINEAR_DRAG), 1e-5f));
    physics_shutdown();
}

// --- Rocks: references, deferred destroy, exit on destroy -------------------------

static void check_rocks(void)
{
    physics_init(record);
    memset(rocks, 0, sizeof rocks);
    player_start(); // the joint's other end

    b2BodyId zone = physics_body(b2_staticBody, (b2Vec2){0, 0}, 0, 0, 0, false);
    physics_circle(zone, (Collider){K_ZONE, 0, TAG_UNTAGGED, true}, (b2Vec2){0, 0}, 0.2025f);

    Rock *rock = rock_instantiate((Vector3){0, 0, ROCKS_POS_Z});
    assert(rock && rock->shadowAlive && !rock->dark);
    rock_set_dark(rock, true);
    assert(rock->dark && rock->renderer.sprite == ROCK_DARK_SPRITE);
    RockRef ref = rock_ref(rock);
    watchedRock = ref;
    logCount = 0;
    steps(1);
    assert(count_events(TRIGGER_ENTER, K_ZONE, OWNER_ROCK) == 1 && count_events(TRIGGER_ENTER, OWNER_ROCK, K_ZONE) == 1);

    // Destroy waits for the end of the frame. Until then the rock exists,
    // and a second Destroy changes nothing.
    rock_destroy(rock);
    rock_destroy(rock);
    assert(rock_get(ref) == rock);
    unsigned generation = rock->generation;
    logCount = 0;
    rocks_end_of_frame();
    // The exit went to both owners at destroy time, while the rock still
    // existed (plan 6.3, item 7).
    assert(logCount == 2 && count_events(TRIGGER_EXIT, K_ZONE, OWNER_ROCK) == 1 &&
           count_events(TRIGGER_EXIT, OWNER_ROCK, K_ZONE) == 1);
    assert(eventLog[0].otherRockAlive && eventLog[1].otherRockAlive);
    // Then the reference reads as empty, and the slot was freed once.
    assert(rock_get(ref) == NULL && rocks[ref.slot].generation == generation + 1);

    // A new rock may take the same slot and the freed Box2D shape index.
    // The old reference stays empty, and Box2D's own late end event for the
    // old shape finds no owner.
    Rock *again = rock_instantiate((Vector3){5, 5, ROCKS_POS_Z});
    assert(again == &rocks[ref.slot] && rock_get(ref) == NULL && rock_get(rock_ref(again)) == again);
    logCount = 0;
    steps(3);
    assert(logCount == 0);
    printf("rocks: destroy at frame end, exits first, old references read empty\n");

    // The pull joint: Box2D holds one only while enabled and connected,
    // and Rock.relocate clears the connection.
    rock_set_joint(again, true, true);
    assert(b2Joint_IsValid(again->joint) && near(b2DistanceJoint_GetLength(again->joint), ROCK_JOINT_DISTANCE, 1e-6f));
    rockDragger.activeJoint = rock_ref(again);
    rockGenerator = (RockGenerator){.numRocks = 0, .worldBounds = {-26, -17.2f, 52.8f, 35.2f}};
    rock_relocate(again);
    assert(B2_IS_NULL(again->joint) && again->jointEnabled && !again->jointConnected);
    assert(rock_get(rockDragger.activeJoint) == NULL && again->destroyQueued && again->shadowDestroyQueued);
    rocks_end_of_frame();
    int alive = 0;
    for (int i = 0; i < MAX_ROCKS; i++) alive += rocks[i].alive;
    assert(alive == 1); // the replacement
    physics_shutdown();
}


// --- Morse and the level layout ---------------------------------------------

// The answers in Morse, written out by hand: a space between letters, a
// slash between words.
static const char *const ANSWERS_IN_MORSE[LEVEL_COUNT - 1] = {
    "--- -.-",                                  // OK
    "-.. . -. -.--",                            // DENY
    ".- -.-. -.-. . ... ...",                   // ACCESS
    ".-. . .- -.. -.--",                        // READY
    ".-. . -../-... .-.. ..-/--. .-. -.",       // RED BLU GRN
    "--- -.-/... .-.. --- .--",                 // OK SLOW
    "-... . - - . .-.",                         // BETTER
};

static void morse_to_text(const MorseWord *words, int count, char *out)
{
    *out = 0;
    for (int w = 0; w < count; w++) {
        if (w) strcat(out, "/");
        for (int i = 0; i < words[w].length; i++) {
            int c = words[w].codes[i];
            strcat(out, c == MORSE_CODE_DOT ? "." : c == MORSE_CODE_DASH ? "-" : " ");
        }
    }
}

static void check_morse(void)
{
    MorseWord words[MORSE_MAX_WORDS];
    char text[256];
    for (int i = 0; i < LEVEL_COUNT - 1; i++) {
        int n = morse_encode(LEVELS[i].decoded, words, MORSE_MAX_WORDS);
        morse_to_text(words, n, text);
        assert(strcmp(text, ANSWERS_IN_MORSE[i]) == 0);
        printf("morse: %-12s %s\n", LEVELS[i].decoded, text);
    }
    // The last message has no answer.
    assert(LEVELS[LEVEL_COUNT - 1].decoded[0] == 0 && morse_encode("", words, MORSE_MAX_WORDS) == 0);
    // Lower case uses the alternate field. A letter with no row gives no
    // codes but keeps its gap. A real double quote never matches (tables.h).
    assert(morse_encode("ok", words, MORSE_MAX_WORDS) == 1);
    morse_to_text(words, 1, text);
    assert(strcmp(text, "--- -.-") == 0);
    morse_encode("A~B\"", words, MORSE_MAX_WORDS);
    morse_to_text(words, 1, text);
    assert(strcmp(text, ".-  -... ") == 0);
}

// One frame as main.c runs it: latched presses are used up.
static void game_frame(void)
{
    unity_frame();
    input_clear();
}

static void frames(int n)
{
    for (int i = 0; i < n; i++) game_frame();
}

// A key down for one frame, as a press on the touch controls does.
static void press(int key)
{
    flx_touch_key(key, true);
    game_frame();
    flx_touch_key(key, false);
    game_frame();
}

static void start_game(unsigned seed)
{
    static const Scene *const table[SCENE_COUNT] = {&GAME_SCENE, &GAME_SCENE, &GAME_SCENE, &GAME_SCENE,
                                                    &GAME_SCENE, &DEATH_SCENE, &WIN_SCENE};
    unity_init(table, SCENE_GAME);
    SetRandomSeed(seed);
    game_frame();
    assert(scene_active() == SCENE_GAME);
}

static void clear_rocks(void)
{
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].alive) rock_destroy(&rocks[i]);
    }
    game_frame();
}

static int live_rocks(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ROCKS; i++) n += rocks[i].alive && !rocks[i].destroyQueued;
    return n;
}

static void check_level_layout(void)
{
    start_game(2);
    const float gx = LEVEL_LOADER_GAP_X, gy = LEVEL_LOADER_GAP_Y;
    assert(gx == 1 && gy == 3 && levelLoader.currentLevel == -1);
    assert(strcmp(levelLoader.currentQuestion, LEVEL_LOADER_INITIAL_QUESTION) == 0);
    for (int level = 0; level < LEVEL_COUNT - 1; level++) {
        assert(level_loader_next_level(-1) && levelLoader.currentLevel == level);
        assert(strcmp(levelLoader.currentQuestion, LEVELS[level].question) == 0);
        // Each word is a line, gap.y apart, centred on the Level object.
        // Each code takes one gap.x slot; letter gaps get no placeholder.
        const char *morse = ANSWERS_IN_MORSE[level];
        int lines = 1;
        for (const char *c = morse; *c; c++) lines += *c == '/';
        float y = (lines + 1) * gy / 2, widest = 0;
        int k = 0;
        for (const char *word = morse; *word; ) {
            int n = (int)strcspn(word, "/");
            y -= gy;
            widest = fmaxf(widest, n * gx);
            for (int i = 0; i < n; i++) {
                if (word[i] == ' ') continue;
                const RockPlaceholder *p = levelLoader.placeholders[k++];
                Vector3 at = p->spriteRenderer.transform.position;
                assert(near(at.x, -n * gx / 2 + gx / 2 + i * gx, 1e-5f) && near(at.y, y, 1e-5f) && at.z == LEVEL_POS_Z);
                assert(rock_placeholder_is_dash(p) == (word[i] == '-'));
                assert(p->spriteRenderer.sprite == (word[i] == '-' ? PLACEHOLDER_DASH_SPRITE : PLACEHOLDER_DOT_SPRITE));
            }
            word += n + (word[n] == '/');
        }
        assert(k == levelLoader.placeholderCount && !level_loader_check());
        assert(levelLoader.sentenceSize.x == widest && levelLoader.sentenceSize.y == (lines + 1) * gy);
        // The decoded letters: one TextMesh per word, each letter followed
        // by a space, hidden until the map view.
        assert(levelLoader.decodedLineCount == lines);
        for (int i = 0; i < lines; i++) {
            const TextMesh *tm = &levelLoader.decodedLines[i];
            assert(!tm->enabled && tm->transform.position.x == LETTERS_POS_X && tm->transform.position.z == LETTERS_POS_Z);
            assert(near(tm->transform.position.y, gy * lines / 2 + LEVEL_LOADER_DECODED_Y_OFFSET - i * gy, 1e-5f));
        }
        game_frame(); // the old placeholders go at the end of a frame
    }
    // Spot checks against the numbers worked out by hand.
    assert(strcmp(levelLoader.decodedLines[0].text, "B E T T E R ") == 0);
    level_loader_next_level(0);
    const RockPlaceholder *first = levelLoader.placeholders[0], *last = levelLoader.placeholders[5];
    assert(first->spriteRenderer.transform.position.x == -3 && first->spriteRenderer.transform.position.y == 0);
    assert(last->spriteRenderer.transform.position.x == 3 && levelLoader.sentenceSize.x == 7);
    assert(strcmp(levelLoader.decodedLines[0].text, "O K ") == 0);
    assert(near(levelLoader.decodedLines[0].transform.position.y, 0.105782f, 1e-6f));
    level_loader_next_level(4);
    assert(levelLoader.placeholderCount == 26 && levelLoader.sentenceSize.x == 13 && levelLoader.sentenceSize.y == 12);
    assert(strcmp(levelLoader.decodedLines[2].text, "G R N ") == 0);
    assert(levelLoader.placeholders[0]->spriteRenderer.transform.position.x == -4 &&
           levelLoader.placeholders[0]->spriteRenderer.transform.position.y == 3);
    // The last message: no placeholders, one empty line, and false.
    assert(!level_loader_next_level(LEVEL_COUNT - 1) && levelLoader.placeholderCount == 0);
    assert(levelLoader.decodedLineCount == 1 && levelLoader.decodedLines[0].text[0] == 0);
    game_frame();
    int alive = 0;
    for (int i = 0; i < MAX_PLACEHOLDERS; i++) alive += rockPlaceholders[i].alive;
    assert(alive == 0);
    printf("layout: 7 answers, placeholders on a %g x %g grid, letters %g apart\n", gx, gy, gy);
}

// --- Placeholders: the 0.6 s clearing push, and correct or wrong --------------------

static Rock *rock_at(float x, float y, bool dark)
{
    Rock *r = rock_generator_add_rock(dark);
    rock_set_position(r, (Vector3){x, y, ROCKS_POS_Z});
    return r;
}

static void check_placeholders(void)
{
    start_game(3);
    press(KEY_SPACE); // the tutorial
    clear_rocks();
    assert(live_rocks() == 0);

    // A rock lying where a dash placeholder appears is pushed away in the
    // placeholder's first 0.6 s: it relocates, and a new rock spawns.
    Rock *early = rock_at(-3, 0, true);
    frames(10);
    RockRef earlyRef = rock_ref(early);
    float created = Time.time;
    level_loader_next_level(0); // "OK": dashes at x -3 -2 -1 1 3, a dot at 2
    frames(2);
    assert(rock_get(earlyRef) == NULL && live_rocks() == 1);
    assert(!rock_placeholder_is_correct(levelLoader.placeholders[0]));
    clear_rocks();

    // One that rolls in just before 0.6 s goes too.
    while (Time.time < created + 0.5f) game_frame();
    RockRef late = rock_ref(rock_at(-2, 0, true));
    frames(2);
    assert(rock_get(late) == NULL);
    clear_rocks();

    // After 0.6 s: the right colour snaps and turns the slot green, the
    // wrong colour turns it red, and leaving resets it.
    while (Time.time < created + 0.62f) game_frame();
    Rock *dark = rock_at(-3, 0, true);
    Rock *light = rock_at(-1, 0, false);
    frames(2);
    RockPlaceholder *p0 = levelLoader.placeholders[0], *p2 = levelLoader.placeholders[2];
    assert(rock_placeholder_is_correct(p0) && rock_get(p0->snappedRock) == dark);
    assert(p2->spriteRenderer.sprite == PLACEHOLDER_INCORRECT_SPRITE && rock_get(p2->snappedRock) == NULL);
    rock_set_position(light, (Vector3){-1, -5, ROCKS_POS_Z});
    frames(2);
    assert(p2->spriteRenderer.sprite == PLACEHOLDER_DASH_SPRITE);
    // A wrong rock next to a snapped one leaves the slot green.
    Rock *wrong = rock_at(-3, 0.1f, false);
    frames(2);
    assert(rock_placeholder_is_correct(p0));
    rock_set_position(wrong, (Vector3){-3, -5, ROCKS_POS_Z});
    frames(2);
    assert(rock_placeholder_is_correct(p0));
    // The snapped rock leaves: back to a dash.
    rock_set_position(dark, (Vector3){-3, 5, ROCKS_POS_Z});
    frames(2);
    assert(p0->spriteRenderer.sprite == PLACEHOLDER_DASH_SPRITE && rock_get(p0->snappedRock) == NULL);
    printf("placeholders: a rock is pushed away before %.1f s, marked after\n", PLACEHOLDER_CLEAR_TIME);

    // The cheat: hold Ctrl, press C. C alone does nothing, and Shift+C
    // toggles the tutorial instead (plan 6.4).
    int before = live_rocks();
    press(KEY_C);
    assert(live_rocks() == before);
    flx_touch_key(KEY_LEFT_SHIFT, true);
    press(KEY_C);
    flx_touch_key(KEY_LEFT_SHIFT, false);
    game_frame();
    assert(live_rocks() == before);
    assert(focus.tutorial.enabled && Time.timeScale == 0);
    press(KEY_SPACE);
    flx_touch_key(KEY_LEFT_CONTROL, true);
    press(KEY_C);
    flx_touch_key(KEY_LEFT_CONTROL, false);
    assert(live_rocks() == before + levelLoader.placeholderCount);
    frames(2);
    assert(level_loader_check());
    printf("cheat: Ctrl+C puts %d rocks on the placeholders, the answer checks\n", levelLoader.placeholderCount);
}

// --- Sol clock, pauses, the message flow, the freeze fix --------------------------

#define SOL_SECONDS (24 * 1.027f * 3600)

// Puts the player's body where a trigger is, or back outside.
static void move_player(float x, float y)
{
    b2Body_SetTransform(player.body, (b2Vec2){x, y}, b2Rot_identity);
    frames(3);
}

static void cheat_now(void)
{
    flx_touch_key(KEY_LEFT_CONTROL, true);
    press(KEY_C);
    flx_touch_key(KEY_LEFT_CONTROL, false);
    frames(2);
}

static void check_sol_clock(void)
{
    // A sol is 24 x 1.027 hours, at 2000 game seconds per second: 44.37 s.
    assert(near(SOL_SECONDS, 88732.8f, 0.01f) && MARS_TIME_SCALE == 2000);
    printf("sol: %.1f game seconds, %.4f s of play\n", SOL_SECONDS, SOL_SECONDS / MARS_TIME_SCALE);

    start_game(4);
    // The first tutorial pauses the game and mutes it, but Earth.Start
    // turns the sound back up (plan 6.3, item 8).
    assert(focus.tutorial.enabled && Time.timeScale == 0 && audio_listener_volume() == 1);
    // Sol 0 at midnight, so the satellite cannot see you.
    assert(mars.currentSol == 0 && mars.darkness == 1 && !mars.isEarthVisible);
    game_frame(); // RoverScreen runs before Mars, so it sees this a frame later
    assert(roverScreen.visibilityIndicator.enabled);
    assert(strcmp(roverScreen.visibilityIndicator.text, "VISIBILITY IS TOO LOW\n50%, WAIT FOR SOL") == 0);
    // Pause path 1: a cutscene sets timeScale 0. Time and the sol clock stop.
    float t0 = Time.time;
    frames(120);
    assert(Time.time == t0 && mars.secondsSpent == 0);
    press(KEY_SPACE);
    assert(Time.timeScale == 1 && !focus.tutorial.enabled);
    // The first moving frame starts sol 1, shows the counter for 3 s and
    // marks the sol for Earth.
    assert(mars.currentSol == 1 && mars.solCounterText.enabled);
    game_frame(); // life support follows the sol a frame later, in any order
    assert(strcmp(mars.solCounterText.text, "SOL 1\n14 LEFT") == 0 && roverScreen.lifeSupport == 14);
    float start = Time.time - UNITY_FRAME_TIME * 2; // the sol clock's zero: two frames have moved
    while (Time.time < start + 3.1f) game_frame();
    assert(!mars.solCounterText.enabled);

    // Decode timing: Earth sees you from a quarter sol (darkness 0.5). Then,
    // before any message, it "decodes" at once and answers after the
    // telemetry delay of 1.8 s.
    while (!focus.newMessage.enabled) game_frame();
    float at = Time.time - start;
    float want = SOL_SECONDS / 4 / MARS_TIME_SCALE + EARTH_TELEMETRY_DELAY;
    printf("message: the first one comes %.3f s after the tutorial (want %.3f)\n", at, want);
    // Each of the two tests waits for the first 60 Hz frame past its time.
    assert(at >= want && at <= want + 2 * UNITY_FRAME_TIME);
    assert(Time.timeScale == 0 && missionUpdateIndicator.enabled && roverNewMessageSound.volume == EARTH_MAX_MESSAGE_SOUND_VOLUME);
    // The sol clock, worked out: secondsSpent is play time times 2000.
    assert(near(mars.secondsSpent, (Time.time - start) * MARS_TIME_SCALE, 70));
    assert(near(mars.darkness, fabsf(mars.secondsSpent / SOL_SECONDS * 2 - 1), 1e-4f) && mars.isEarthVisible);
    assert(!roverScreen.visibilityIndicator.enabled);
    press(KEY_SPACE);

    // Into the rover: the screen goes on and the first message loads.
    move_player(ROVER_INSIDE_TRIGGER_POS_X, ROVER_INSIDE_TRIGGER_POS_Y);
    assert(focus.playerIsInRover && roverScreen.on && roverScreen.screenGUIText.enabled && mars.timeFrozen);
    assert(levelLoader.currentLevel == 0 && levelLoader.placeholderCount == 6 && !missionUpdateIndicator.enabled);
    const char *head = "EARTH:\n------\nConnection seems \nto be broken.";
    assert(strncmp(roverScreen.screenGUIText.text, head, strlen(head)) == 0);
    // Pause path 2: in the rover, time runs, but the sol clock stops.
    float spent = mars.secondsSpent;
    t0 = Time.time;
    frames(120);
    assert(Time.time > t0 + 1.9f && mars.secondsSpent == spent);

    // The answer: the cheat, then Earth decodes and shows the map for 2 s.
    cheat_now();
    assert(level_loader_check() && focus.overViewForced);
    frames(2);
    // The letters show in the map view, bright because the answer is right.
    assert(levelLoader.decodedLines[0].enabled && levelLoader.decodedLines[0].color.a == LEVEL_LOADER_LETTERS_ALPHA_CORRECT);
    // The freeze fix: a tutorial key during the overview does nothing.
    press(KEY_H);
    assert(!focus.tutorial.enabled && Time.timeScale == 1);
    float overview = Time.time;
    while (!focus.couldDecode.enabled && Time.time < overview + 5) game_frame();
    assert(focus.couldDecode.enabled && !focus.overViewForced && Time.timeScale == 0);
    printf("freeze fix: H in the overview is ignored, the decoded screen comes %.2f s later\n", Time.time - overview);
    frames(2);
    assert(!levelLoader.decodedLines[0].enabled);
    // Outside the overview H, Escape, P and Shift toggle the tutorial. It
    // hides the decoded screen too, and the second press resumes.
    press(KEY_H);
    assert(focus.tutorial.enabled && !focus.couldDecode.enabled && Time.timeScale == 0);
    press(KEY_P);
    assert(!focus.tutorial.enabled && Time.timeScale == 1);
    press(KEY_ESCAPE);
    assert(focus.tutorial.enabled);
    press(KEY_RIGHT_SHIFT);
    assert(!focus.tutorial.enabled && Time.timeScale == 1);

    // The next message, 1.8 s after the decoded screen.
    while (!focus.newMessage.enabled) game_frame();
    // The kept showCutscene bug: the tutorial over a new message leaves both on.
    press(KEY_H);
    assert(focus.tutorial.enabled && focus.newMessage.enabled);
    press(KEY_SPACE);
    assert(!focus.tutorial.enabled && !focus.newMessage.enabled);

    // Out of the rover and back: the clock runs on from where it stopped,
    // and the next message loads.
    move_player(ROVER_INSIDE_TRIGGER_POS_X - 4, ROVER_INSIDE_TRIGGER_POS_Y - 3);
    assert(!focus.playerIsInRover && !mars.timeFrozen && !roverScreen.on);
    assert(mars.secondsSpent > spent && mars.secondsSpent < spent + 4 * UNITY_FRAME_TIME * MARS_TIME_SCALE);
    move_player(ROVER_INSIDE_TRIGGER_POS_X, ROVER_INSIDE_TRIGGER_POS_Y);
    assert(levelLoader.currentLevel == 1);

    // The rest of the game with the cheat, to the rocket.
    while (levelLoader.currentLevel < LEVEL_COUNT - 1) {
        frames(40); // past the 0.6 s clearing push
        cheat_now();
        while (!focus.couldDecode.enabled) game_frame();
        press(KEY_SPACE);
        while (!focus.newMessage.enabled) game_frame();
        press(KEY_SPACE);
        move_player(ROVER_INSIDE_TRIGGER_POS_X - 4, ROVER_INSIDE_TRIGGER_POS_Y - 3);
        move_player(ROVER_INSIDE_TRIGGER_POS_X, ROVER_INSIDE_TRIGGER_POS_Y);
    }
    assert(levelLoader.placeholderCount == 0 && strcmp(levelLoader.currentQuestion, LEVELS[LEVEL_COUNT - 1].question) == 0);
    assert(mars.currentSol == 1); // all of it in the first sol, from the rover
    // The rocket is down. Walking into it wins.
    move_player(ROCKET_INSIDE_TRIGGER_POS_X, ROCKET_INSIDE_TRIGGER_POS_Y);
    assert(scene_active() == SCENE_WIN);
    printf("game: all %d messages with the cheat, then the rocket, then Win\n", LEVEL_COUNT);
}

// --- The rover screen text, and life support running out ---------------------------

static void check_rover_screen(void)
{
    start_game(5);
    level_loader_next_level(0);
    roverScreen.lifeSupport = 9;
    mars.hoursUntilNextSol = 7.9f;
    mars.secondsUntilNextSol = 7.9f * 3600; // 474 minutes: 54 past the hour
    mars.isEarthVisible = true;
    mars.darkness = 0.3f;
    // Worked out from RoverScreen.cs by a separate model (step 5-6 notes).
    // The wrap counts the header's newlines, so the first line of the
    // message is short.
    const char *want =
        "EARTH:\n"
        "------\n"
        "Connection seems \n"
        "to be broken. We can send messa\n"
        "ges to you, but you won't be ab\n"
        "le to reply. You have to answer\n"
        "us by arranging rocks in morse \n"
        "code. Good luck! Write as short\n"
        "and fast as you can. You are   \n"
        "short on life support.\n"
        "\n"
        "\n"
        "_______________________________\n"
        "\n"
        "LIFE SUPPORT LEFT:      09 SOLS\n"
        "HOURS UNTIL NEXT SOL:     07:54\n"
        "SATELITE IMAGERY COVERAGE:  YES";
    assert(strcmp(rover_screen_print_screen(), want) == 0);
    assert(!roverScreen.visibilityIndicator.enabled);
    // Night: " NO", and the darkness percentage, 50 to 100.
    mars.isEarthVisible = false;
    mars.darkness = 0.73f;
    const char *text = rover_screen_print_screen();
    assert(strstr(text, "COVERAGE:   NO") && roverScreen.visibilityIndicator.enabled);
    assert(strcmp(roverScreen.visibilityIndicator.text, "VISIBILITY IS TOO LOW\n77%, WAIT FOR SOL") == 0);
    mars.darkness = 1;
    rover_screen_print_screen();
    assert(strcmp(roverScreen.visibilityIndicator.text, "VISIBILITY IS TOO LOW\n50%, WAIT FOR SOL") == 0);
    // Rounding halves to even, as Mathf.Round does.
    mars.darkness = 0.875f; // 62.5
    rover_screen_print_screen();
    assert(strcmp(roverScreen.visibilityIndicator.text, "VISIBILITY IS TOO LOW\n62%, WAIT FOR SOL") == 0);
    printf("rover screen: the first message's text matches, with the kept wrapping bug\n");

    // Life support: 14 + 1 sols. The player dies when it would go below 0,
    // in sol 16, after 15 sols of play outside the rover and cutscenes.
    press(KEY_SPACE);
    clear_rocks();
    float start = Time.time;
    while (scene_active() == SCENE_GAME && Time.time < start + 700) {
        if (Time.timeScale == 0) press(KEY_SPACE); // the messages
        else game_frame();
        assert(roverScreen.lifeSupport == ROVER_SCREEN_STARTING_LIFE_SUPPORT + 1 - mars.currentSol ||
               roverScreen.lifeSupport == ROVER_SCREEN_STARTING_LIFE_SUPPORT + 2 - mars.currentSol);
    }
    float died = Time.time - start;
    printf("life support: Death loads after %.2f s of play (15 sols: %.2f s)\n", died, 15 * SOL_SECONDS / MARS_TIME_SCALE);
    assert(scene_active() == SCENE_DEATH && near(died, 15 * SOL_SECONDS / MARS_TIME_SCALE, 0.1f));
}

// --- The Game scene: movement ------------------------------------------------------

static void check_movement(void)
{
    const Scene *table[SCENE_COUNT] = {&GAME_SCENE, &GAME_SCENE, &GAME_SCENE, &GAME_SCENE,
                                       &GAME_SCENE, &GAME_SCENE, &GAME_SCENE};
    unity_init(table, SCENE_GAME);
    SetRandomSeed(1);
    game_frame();
    assert(scene_active() == SCENE_GAME);
    // The first tutorial pauses the game.
    assert(focus.tutorial.enabled && Time.timeScale == 0);
    int count = 0;
    for (int i = 0; i < MAX_ROCKS; i++) count += rocks[i].alive;
    assert(count == ROCK_GENERATOR_NUM_ROCKS);
    assert(near(mars.worldBunds.width, 52.8f, 1e-4f) && near(mars.worldBunds.height, 35.2f, 1e-4f));
    // Clear the rocks out of the way.
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].alive) rock_destroy(&rocks[i]);
    }
    // GetAxis uses scaled time: an arrow held through the pause leaves the
    // axis at 0 (measured on the 2016 WebGL build).
    flx_touch_key(KEY_LEFT, true);
    for (int i = 0; i < 30; i++) game_frame();
    assert(input_get_axis(AXIS_HORIZONTAL) == 0);
    flx_touch_key(KEY_LEFT, false);
    flx_touch_key(KEY_SPACE, true);
    game_frame();
    flx_touch_key(KEY_SPACE, false);
    game_frame();
    assert(!focus.tutorial.enabled && Time.timeScale == 1);

    // Hold left (the rover's roof is just to the right): GetAxis rises by
    // 0.05 per frame, and every fixed step sets the velocity to
    // 0.8 * axis * 7.2.
    float x0 = player_position().x;
    flx_touch_key(KEY_LEFT, true);
    int frames = 0;
    while (b2Body_GetLinearVelocity(player.body).x > -5.76f + 1e-4f && frames < 120) {
        game_frame();
        frames++;
    }
    printf("movement: full speed %.3f units/s after %d frames\n", b2Body_GetLinearVelocity(player.body).x, frames);
    assert(frames >= 20 && frames <= 22);
    float x1 = player_position().x;
    for (int i = 0; i < 60; i++) game_frame();
    float speed = x1 - player_position().x;
    printf("movement: %.4f units in the next second\n", speed);
    assert(near(speed, 5.76f, 0.02f));
    assert(x1 < x0 && player.renderer.transform.flipped && player.animator.state == 0); // run
    flx_touch_key(KEY_LEFT, false);
    for (int i = 0; i < 30; i++) game_frame();
    assert(b2Body_GetLinearVelocity(player.body).x == 0 && player.animator.state == 2); // idle
    // Right faces right.
    flx_touch_key(KEY_RIGHT, true);
    for (int i = 0; i < 30; i++) game_frame();
    flx_touch_key(KEY_RIGHT, false);
    assert(!player.renderer.transform.flipped);
    // The camera follows: SmoothDamp of 0.01 s keeps it within a few
    // hundredths of the player.
    assert(fabsf(focus.camera.position.x - player_position().x) < 0.1f);
    for (int i = 0; i < 30; i++) game_frame();
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    check_hulls();
    check_physics_bridge();
    check_rocks();
    check_movement();
    check_morse();
    check_level_layout();
    check_placeholders();
    check_sol_clock();
    check_rover_screen();
    check_axis();
    check_round();
    check_smooth_damp();
    check_time();
    check_scenes();
    check_mouse();
    printf("All checks passed.\n");
    return 0;
}
