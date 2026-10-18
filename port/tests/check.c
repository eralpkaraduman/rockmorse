// Logic checks that need no window. Run from the port folder, so the font
// in assets/ is found:
//   ./build/rockmorse_check
#include "../src/scenes.h"
#include "../src/game.h"
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

// --- The Game scene: movement ------------------------------------------------------

// One frame as main.c runs it: latched presses are used up.
static void game_frame(void)
{
    unity_frame();
    input_clear();
}

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
    check_axis();
    check_round();
    check_smooth_damp();
    check_time();
    check_scenes();
    check_mouse();
    printf("All checks passed.\n");
    return 0;
}
