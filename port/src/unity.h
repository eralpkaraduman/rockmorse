// unity: the small part of Unity 5.4 that Rockmorse's scripts use, on raylib.
// It has no component model. Each scene keeps its objects as plain structs
// and calls these functions, so the game code reads like the C# it came from.
// Names follow Unity where that helps the comparison, for example Time.time.
#ifndef ROCKMORSE_UNITY_H
#define ROCKMORSE_UNITY_H

#include <stdbool.h>
#include "raylib.h"
#include "values.h"

// --- Scenes -------------------------------------------------------------

// Build order, settings/EditorBuildSettings.json. LoadScene(0) is Intro.
typedef enum {
    SCENE_INTRO,
    SCENE_TITLE,
    SCENE_STORY1,
    SCENE_STORY2,
    SCENE_GAME,
    SCENE_DEATH,
    SCENE_WIN,
    SCENE_COUNT
} SceneId;

// One scene's scripts. Any hook may be NULL. The frame loop calls them in
// Unity's order, see unity_frame.
typedef struct {
    const char *name;           // the name LoadScene takes, e.g. "Story1"
    void (*start)(void);        // load the scene: Awake, then Start, of every script
    void (*fixed_update)(void); // FixedUpdate, 50 times per second of game time
    void (*physics)(void);      // physics step, then trigger and collision handlers
    void (*update)(void);       // Update, once per frame
    void (*late_update)(void);  // LateUpdate, once per frame
    void (*end_of_frame)(void); // deferred Destroy calls
    void (*draw)(void);         // camera, sprites, then GUI elements
} Scene;

// Name of a scene, and the scene with a name (-1 when none has it).
const char *scene_name(SceneId id);
int scene_find(const char *name);

// SceneManager.LoadScene. Like Unity, the switch waits for the end of the
// frame, so the rest of the frame still runs in the old scene.
void scene_load(SceneId id);
void scene_load_name(const char *name);
SceneId scene_active(void);

// --- Frame loop ---------------------------------------------------------

// Update runs at 60 Hz, like the 2016 WebGL build in a browser, whatever
// the display rate. Several effects ease by a fixed fraction per frame.
#define UNITY_FRAME_RATE 60
#define UNITY_FRAME_TIME (1.0f / UNITY_FRAME_RATE)

// The scene table, indexed by SceneId, and the scene to start in.
void unity_init(const Scene *const *scenes, SceneId first);

// One Unity frame: time, input axes, scene switch, fixed updates with
// physics, Update, LateUpdate, deferred destroys. Drawing is separate.
void unity_frame(void);
void unity_draw(void);

// --- Time ---------------------------------------------------------------

// Unity's Time. time counts game time from the start of the app and never
// resets on a scene change. timeScale also carries over. At timeScale 0,
// time stops and deltaTime is 0, but Update still runs. Inside FixedUpdate
// and the physics handlers, time is the fixed time and deltaTime is
// fixedDeltaTime, as Unity documents.
typedef struct {
    float time;
    float deltaTime;
    float unscaledDeltaTime;
    float fixedDeltaTime;
    float timeScale;
} UnityTime;
extern UnityTime Time;

// --- Input --------------------------------------------------------------

// Keys use raylib key codes. A key counts as down if the keyboard, a touch
// button or a gamepad holds it. Presses and releases are latched per
// rendered frame and consumed by the next Unity frame, so none is lost or
// seen twice. The gamepad D-pad sends the arrows, A sends Space, the left
// shoulder sends Tab and Start sends H (plan section 6.4).
void input_poll(void);  // once per rendered frame
void input_clear(void); // after each Unity frame
bool input_get_key(int key);
bool input_get_key_down(int key);
bool input_get_key_up(int key);

// Mouse buttons are raylib's. The position is in game pixels with Unity's
// origin, the bottom left corner. main.c sets the mapping from the window.
bool input_get_mouse_button(int button);
bool input_get_mouse_button_down(int button);
bool input_get_mouse_button_up(int button);
Vector2 input_mouse_position(void);
void input_set_mouse_mapping(Vector2 offset, float scale);

// Input.GetAxis("Horizontal") and ("Vertical"). Arrows and WASD, smoothed
// with Unity's rules (settings/InputManager.json). A gamepad's left stick
// stays analog: GetAxis returns whichever is larger, the keys or the stick.
typedef enum { AXIS_HORIZONTAL, AXIS_VERTICAL, AXIS_COUNT } Axis;
float input_get_axis(Axis axis);

// The smoothing of one key axis over one frame, kept apart for the checks.
// positive and negative are whether those keys are held.
float axis_smooth(float value, bool positive, bool negative, float dt,
                  float sensitivity, float gravity, bool snap);
float axis_dead_zone(float value, float dead);

// Called by the on-screen touch controls on the web (touch-controls.js)
// with a raylib key code, when a button goes down or up.
void flx_touch_key(int key, bool down);

// --- Maths --------------------------------------------------------------

// Mathf.Round: halves round to even, like C#'s Math.Round.
float mathf_round(float f);
// Mathf.Lerp and Mathf.Clamp, as Unity 5.4 has them (INVENTORY.md 2.21).
float mathf_lerp(float a, float b, float t);
float mathf_clamp(float value, float min, float max);
// Mathf.SmoothDamp, six argument form, exactly as Unity 5.4 has it.
float mathf_smooth_damp(float current, float target, float *currentVelocity,
                        float smoothTime, float maxSpeed, float deltaTime);
// Random.Range. The int form excludes max, the float form includes it.
// The numbers come from raylib, so they differ from Unity's stream.
int random_range_int(int min, int max);
float random_range_float(float min, float max);

// --- Transforms and colours -----------------------------------------------

typedef struct { float r, g, b, a; } Rgba;
#define RGBA_WHITE ((Rgba){1, 1, 1, 1})

// World position and scale. angle is the rotation around Z in degrees,
// counter-clockwise. flipped is a half turn around Y: the game faces left
// by writing 180 into a quaternion component, and the port copies the
// effect, not the maths.
typedef struct {
    Vector3 position;
    Vector3 scale;
    float angle;
    bool flipped;
} UnityTransform;

// Moves a child that follows a moving parent, from its local values.
void transform_follow(UnityTransform *child, const UnityTransform *parent, Vector3 localPosition, Vector3 localScale);

// --- Camera and sprites ------------------------------------------------------

// The one orthographic camera. orthographicSize is half the view height in
// world units, and the view is the 660 x 440 game screen.
typedef struct {
    Vector3 position;
    float orthographicSize;
    Rgba background;
} UnityCamera;

// Clears the screen with the camera's background colour.
void camera_clear(const UnityCamera *camera);
// World point to game screen pixels, origin top left.
Vector2 camera_world_to_screen(const UnityCamera *camera, Vector3 world);

typedef struct {
    UnityTransform transform;
    int sprite; // SPR_* from asset_tables.h, -1 for none
    Rgba color;
    int sortingOrder;
    bool enabled;
    // A flat plane of one colour instead of a sprite, as the Game scene's
    // night overlay MeshRenderer: transform.scale.x by .y world units,
    // centred on the position.
    bool solid;
} SpriteRenderer;

// Draws sprites like Unity: sorting order first, then far to near (larger z
// first, the camera looks along +z). Equal keys keep the list order.
void draw_sprites(const UnityCamera *camera, SpriteRenderer *const *sprites, int count);

// --- Sprite animation ----------------------------------------------------

// The Animator of Sprites/run/Player.controller: three states, each one
// sprite clip, no transitions (asset_tables.h, ANIM_STATES). It starts in
// the default state at time 0.
typedef struct {
    int state; // index into ANIM_STATES
    float time;
} Animator;

Animator animator_new(void);
// Animator.Play(name). Playing the state that is already playing does
// nothing, so it keeps its time. The game calls it every fixed update.
void animator_play(Animator *a, const char *state);
// Advances the clip. Unity does this after Update, by Time.deltaTime.
void animator_update(Animator *a, float deltaTime);
// The sprite the clip shows now.
int animator_sprite(const Animator *a);

// --- GUI elements --------------------------------------------------------

// Unity's TextAnchor, also used for GUIText.alignment (left 0, centre 1, right 2).
enum {
    ANCHOR_UPPER_LEFT, ANCHOR_UPPER_CENTER, ANCHOR_UPPER_RIGHT,
    ANCHOR_MIDDLE_LEFT, ANCHOR_MIDDLE_CENTER, ANCHOR_MIDDLE_RIGHT,
    ANCHOR_LOWER_LEFT, ANCHOR_LOWER_CENTER, ANCHOR_LOWER_RIGHT
};

#define GUI_TEXT_MAX 1024

// Screen-space text in the Joystix font. position is in viewport fractions
// (0 to 1, origin bottom left). Layout measured on the 2016 build, see
// unity.c, gui_text_layout.
typedef struct {
    char text[GUI_TEXT_MAX];
    Vector3 position;
    int fontSize;
    int anchor;
    int alignment;
    Vector2 pixelOffset;
    float lineSpacing;
    Rgba color;
    bool enabled;
} GUIText;

void gui_text_set(GUIText *t, const char *text);
void gui_text_draw(const GUIText *t);
// The text block in game pixels, origin top left: what the mouse hits.
Rectangle gui_text_screen_rect(const GUIText *t);

// Unity's OnMouseEnter, OnMouseExit and OnMouseUp for one GUI element.
// OnMouseUp goes to the element that got the mouse down, wherever the
// button is released.
typedef struct { bool over, pressed; } MouseTarget;
typedef struct { bool enter, exit, up; } MouseEvents;
MouseEvents mouse_target_update(MouseTarget *target, bool over, bool buttonDown, bool buttonUp);
MouseEvents gui_text_mouse_events(const GUIText *t, MouseTarget *target);

// A screen image. position and scale are viewport fractions; the rectangle
// is the scaled screen plus the pixel inset. Colour 0.5 means no tint.
typedef struct {
    int image; // IMG_* from asset_tables.h
    Vector3 position;
    Vector3 scale;
    Rectangle pixelInset;
    Rgba color;
    bool enabled;
} GUITexture;

void gui_texture_draw(const GUITexture *t);

// World-space text. Unity sizes it by the font's pixel scale, which is in
// extract/build/font.json (m_PixelScale) but not yet in values.h, so the
// scene sets pixelScale itself (level_loader.c).
typedef struct {
    char text[GUI_TEXT_MAX];
    UnityTransform transform;
    int fontSize;
    float characterSize;
    float pixelScale;
    int anchor;
    int alignment;
    Rgba color;
    bool enabled;
} TextMesh;

void text_mesh_draw(const UnityCamera *camera, const TextMesh *t);

// --- Animation curves ------------------------------------------------------

// AnimationCurve.Evaluate for keys stored as values.h stores them: per key
// inSlope, outSlope, tangentMode, time, value. Hermite between keys, and
// the end values outside them (the clamp wrap mode every curve here has).
float curve_evaluate(const float *keys, int count, float time);

// --- Audio -----------------------------------------------------------------

// Unity's AudioRolloffMode, the values of *_ROLLOFF_MODE in values.h.
enum { ROLLOFF_LOGARITHMIC, ROLLOFF_LINEAR, ROLLOFF_CUSTOM };

// An audio source. A source loads its sound when it first plays. A scene
// change stops every source, as the 2016 build does (measured: the title's
// click sound stops 19 ms after the click, when Story1 loads).
typedef struct {
    int clip; // SND_* from asset_tables.h
    float volume;
    float pitch;
    bool loop;
    bool playOnAwake;
    bool enabled;
    // 3D sound. spatialBlend 0 is a plain 2D sound, 1 a 3D one. The game
    // keeps position up to date for 3D sources.
    float spatialBlend;
    float minDistance;
    float maxDistance;
    int rolloffMode;
    const float *rolloffCurve; // ROLLOFF_CUSTOM: two keys, as curve_evaluate takes them
    float dopplerLevel;
    Vector3 position;
    // Runtime. A looping source streams, so it can loop without a gap.
    Sound sound;
    Music music;
    bool loaded;
    Vector3 lastPosition; // for the Doppler velocity
    bool hasLastPosition;
} AudioSource;

// Unity's Awake for a source: plays it when enabled and playOnAwake.
void audio_source_awake(AudioSource *s);
void audio_source_play(AudioSource *s);
void audio_source_stop(const AudioSource *s);
bool audio_source_is_playing(const AudioSource *s);
// AudioSource.enabled. Turning it on from off starts a playOnAwake source.
// Setting it to the value it has does nothing.
void audio_source_set_enabled(AudioSource *s, bool enabled);
// Pushes volume, pan and pitch to the sound, and feeds a looping stream.
void audio_source_update(AudioSource *s);
// Destroying the source's object: stops and unloads it.
void audio_source_destroy(AudioSource *s);

// AudioListener.volume, a global that survives scene loads.
void audio_listener_set_volume(float volume);
float audio_listener_volume(void);

// The AudioListener's world position, and whether it is turned half way
// round Y. Turned, its right ear points to -x. The Game scene sets it every
// frame; the other scenes have only 2D sounds.
void audio_listener_set(Vector3 position, bool turned);

// What a source sounds like for the listener: the volume and pan raylib
// gets, and the pitch with the Doppler shift. Velocities are in units per
// second. The checks call this with known positions.
typedef struct { float volume, pan, pitch; } AudioMix;
AudioMix audio_mix(const AudioSource *s, Vector3 listener, bool turned, Vector3 listenerVelocity,
                   Vector3 sourceVelocity);
// The distance fade of a 3D source, Unity's rolloff curves.
float audio_rolloff(const AudioSource *s, float distance);

// --- Misc ---------------------------------------------------------------

// Application.OpenURL.
void application_open_url(const char *url);

#endif
