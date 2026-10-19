// The Game scene: its objects and the scripts on them, shared between the
// script files. game.c runs them in one fixed order (plan section 6.3,
// item 10), and handles the physics events in one switch.
#ifndef ROCKMORSE_GAME_H
#define ROCKMORSE_GAME_H

#include <math.h>
#include <string.h>
#include "scenes.h"
#include "physics.h"
#include "code_constants.h"

// Unity's tag for a GameObject without one. The inside triggers of the
// rover and the rocket, rocks and placeholders have it.
#define TAG_UNTAGGED "Untagged"

// Builds a SpriteRenderer from a values.h prefix, e.g. SPRITE_RENDERER(ROVER_BODY).
#define SPRITE_RENDERER(p) ((SpriteRenderer){ \
    .transform = {.position = {p##_POS_X, p##_POS_Y, p##_POS_Z}, .scale = {p##_SCALE_X, p##_SCALE_Y, p##_SCALE_Z}}, \
    .sprite = p##_SPRITE, .color = {p##_COLOR_R, p##_COLOR_G, p##_COLOR_B, p##_COLOR_A}, \
    .sortingOrder = p##_SORTING_ORDER, .enabled = p##_ENABLED})

// Who owns a collider, for the event switch in game.c.
typedef enum {
    OWNER_PLAYER,
    OWNER_ROCK,
    OWNER_ROVER_BODY,
    OWNER_ROVER_ROOF,
    OWNER_ROVER_REPELLER,
    OWNER_ROVER_INSIDE,
    OWNER_ROCKET_BODY,
    OWNER_ROCKET_COVER,
    OWNER_ROCKET_REPELLER,
    OWNER_ROCKET_INSIDE,
    OWNER_PLACEHOLDER,
} Owner;

// Unity's Rect.
typedef struct { float x, y, width, height; } Rect;

// Sprites to draw this frame. Unity sorts them, see draw_sprites.
#define GAME_MAX_SPRITES 16384
typedef struct {
    SpriteRenderer *items[GAME_MAX_SPRITES];
    int count;
} SpriteList;
void sprite_list_add(SpriteList *list, SpriteRenderer *r);

// --- Mars (mars.c) ---------------------------------------------------------

typedef struct {
    Rect worldBunds; // spelling as in the source
    float secondsSpent;
    float secondsUntilNextSol;
    float hoursUntilNextSol;
    float percentTimeLeftForNextSol;
    int currentSol;
    float darkness;
    bool isEarthVisible;
    bool timeFrozen;
    GUIText solCounterText;
} Mars;
extern Mars mars;
void mars_start(void);
void mars_update(void);
void mars_sprites(SpriteList *list);

// --- Rocks (rock.c, rock_generator.c) ----------------------------------------

#define MAX_ROCKS 256

// A reference to a rock. A destroyed rock reads as gone, as Unity's
// destroyed objects compare equal to null, because its slot's generation
// has moved on.
typedef struct {
    int slot;
    unsigned generation;
} RockRef;
#define ROCK_NONE ((RockRef){-1, 0})

typedef struct {
    bool alive;
    bool destroyQueued;
    unsigned generation;
    b2BodyId body;
    int collider;
    SpriteRenderer renderer;
    bool dark;
    AudioSource dragSFXSource;
    SpriteRenderer shadow; // Rock.shadow, a sibling object
    bool shadowAlive;
    bool shadowDestroyQueued;
    // The DistanceJoint2D. Box2D holds a joint only while it is enabled
    // and has a connected body.
    bool jointEnabled;
    bool jointConnected;
    b2JointId joint;
} Rock;
extern Rock rocks[MAX_ROCKS];

Rock *rock_get(RockRef ref); // NULL once the rock is destroyed
RockRef rock_ref(const Rock *rock);
Vector2 rock_position(const Rock *rock);
// Instantiate(rockPrefab, position, identity), then the rock's Start.
Rock *rock_instantiate(Vector3 position);
void rock_set_dark(Rock *rock, bool value);
void rock_update(Rock *rock);
void rock_relocate(Rock *rock);
void rock_on_event(Rock *rock, PhysicsEvent e, const Collider *other);
// DistanceJoint2D.connectedBody (the player or none) and .enabled.
void rock_set_joint(Rock *rock, bool enabled, bool connected);
// Destroy(gameObject): the rock goes at the end of the frame.
void rock_destroy(Rock *rock);
void rocks_end_of_frame(void);
void rocks_sync(void);
void rocks_sprites(SpriteList *list);

typedef struct {
    int numRocks;
    Rect worldBounds;
} RockGenerator;
extern RockGenerator rockGenerator;
void rock_generator_start(void);
void rock_generator_generate(Rect worldBounds);
void rock_generator_replace(Rock *rock);
Rock *rock_generator_add_rock(bool dark);
// transform.position of a rock: moves its body too.
void rock_set_position(Rock *rock, Vector3 position);

// --- RockShatterPrefab (effects.c, with TimedObjectDestructor.js) ----------------

// One particle a frame for 0.5 s, 30 at 60 Hz; room for 100 a second.
#define SHATTER_MAX_PARTICLES 64
#define MAX_SHATTERS 128
typedef struct {
    Vector2 position;
    Vector2 velocity;
    float energy; // seconds left
    float size;   // world units across
    float rotation;
} Particle;

typedef struct {
    bool alive;
    Vector3 position;
    float time; // game time since Instantiate: the Animation clip and the Invoke
    bool emit;  // ParticleEmitter.emit, set by the Animation clip
    Particle particles[SHATTER_MAX_PARTICLES];
    int particleCount;
    SpriteRenderer sprites[SHATTER_MAX_PARTICLES]; // filled for drawing
    AudioSource sound; // Explosion1
} Shatter;
extern Shatter shatters[MAX_SHATTERS];
Shatter *shatter_instantiate(Vector3 position);
void shatter_update(Shatter *s, float deltaTime);
void shatters_update(void);
void shatters_reset(void);
void shatters_sprites(SpriteList *list);

// --- Player (player.c: TopDownControls and the Player object) --------------

typedef struct {
    b2BodyId body;
    int triggerCollider; // the big circle that finds rocks
    int bodyCollider;    // the solid polygon
    SpriteRenderer renderer;
    Animator animator;
    AudioSource footStepsSFX;
    AudioSource strainSFX; // RockDragger.strainSFX, the Player's own source
    AudioSource ambience;  // pivot/Sci-fi_AmbienceLoop1
    // pivot, which holds the audio listener and the ambience: turned half
    // way round Y while the player faces right, the other way to the player.
    bool listenerTurned;
    float walkSpeed;
    float sprintSpeed;
    float curSpeed;
    float maxSpeed;
} Player;
extern Player player;
void player_start(void);
void player_fixed_update(void);
void player_sync(void);
Vector3 player_position(void);
// Moves the AudioListener and the Player's sounds to where the player is.
void player_audio_update(void);

// --- RockDragger (rock_dragger.c) ---------------------------------------------

#define MAX_PULL_CANDIDATES 64
typedef struct {
    RockRef pullCandidate;
    RockRef activeJoint; // the rock whose joint pulls
    RockRef pullCandidates[MAX_PULL_CANDIDATES];
    int pullCandidateCount;
} RockDragger;
extern RockDragger rockDragger;
void rock_dragger_start(void);
void rock_dragger_update(void);
void rock_dragger_on_event(PhysicsEvent e, const Collider *other);

// --- SnapToTarget (drag_target.c) ----------------------------------------------

typedef struct {
    SpriteRenderer renderer;
    RockRef target;
    bool isDash;
} SnapToTarget;
extern SnapToTarget dragTarget;
void drag_target_start(void);
void drag_target_update(void);

// --- Camera (camera_focus.c: CamerFocusSwitcher, CutsSceneScaleFixer) -------

typedef struct {
    UnityCamera camera;
    Vector3 positionTarget;
    bool playerIsInRover;
    bool overViewForced;
    float defaultCameraSize;
    SpriteRenderer nightosphere;
    // Main Camera/cutscenes and its four images. Earth shows them.
    UnityTransform cutscenes;
    float cutscenesDefaultScale;
    float cutscenesDefaultCameraScale;
    SpriteRenderer couldNotDecode, newMessage, couldDecode, tutorial;
} CameraFocus;
extern CameraFocus focus;
void camera_focus_start(void);
void camera_focus_update(void);
void camera_focus_late_update(void);
void camera_focus_force_overview(bool forced);
void camera_focus_sprites(SpriteList *list);
void camera_focus_cutscene_sprites(SpriteList *list);

// --- Rover (rover.c: Rover, RoverInsideTrigger, VisibleOnTrigger) ------------

void rover_start(void);
void rover_on_event(Owner owner, PhysicsEvent e, const Collider *other);
void rover_sprites(SpriteList *list);
Vector3 rover_focus_target(void);
// Rover/updatesIndicator, which Earth drives.
extern SpriteRenderer missionUpdateIndicator;

// --- Rocket (rocket.c: Rocket, InsideRocketTrigger, VisibleOnTrigger) --------

// Instantiate(rocketPrefab) at its saved position. Earth calls it for the
// last level.
void rocket_spawn(void);
void rocket_reset(void);
void rocket_on_event(Owner owner, PhysicsEvent e, const Collider *other);
void rocket_sprites(SpriteList *list);

// --- Morse (morse.c) ------------------------------------------------------------

// Morse.encode: one list of codes per word, MORSE_CODE_GAP between letters.
#define MORSE_MAX_WORDS 8
#define MORSE_MAX_CODES 64
typedef struct {
    int codes[MORSE_MAX_CODES];
    int length;
} MorseWord;
int morse_encode(const char *sentence, MorseWord *words, int maxWords);

// --- RockPlaceholder (rock_placeholder.c) -------------------------------------

// One dot or dash slot of the answer, a RockPlaceholderPrefab instance.
#define MAX_PLACEHOLDERS 64
typedef struct {
    bool alive;
    bool destroyQueued;
    SpriteRenderer spriteRenderer;
    AudioSource correctAudioSource;
    bool _isDash;
    RockRef snappedRock;
    float markTimeCreated;
    float clearTime;
    b2BodyId body;
} RockPlaceholder;
extern RockPlaceholder rockPlaceholders[MAX_PLACEHOLDERS];
// Instantiate(placeholderPrefab, position, identity), then its Start.
RockPlaceholder *rock_placeholder_instantiate(Vector3 position);
void rock_placeholder_set_dash(RockPlaceholder *p, bool dash);
bool rock_placeholder_is_dash(const RockPlaceholder *p);
bool rock_placeholder_is_correct(const RockPlaceholder *p);
void rock_placeholder_on_event(RockPlaceholder *p, PhysicsEvent e, const Collider *other);
void rock_placeholder_destroy(RockPlaceholder *p);
void rock_placeholders_reset(void);
void rock_placeholders_end_of_frame(void);
void rock_placeholders_sprites(SpriteList *list);

// --- LevelLoader (level_loader.c) -------------------------------------------------

typedef struct {
    int currentLevel;
    Vector3 position; // the Level object
    Vector2 gap;
    Vector2 sentenceSize;
    const char *currentQuestion;
    RockPlaceholder *placeholders[MAX_PLACEHOLDERS];
    int placeholderCount;
    // decodedLines: one TextMesh per word of the answer, under Level/letters.
    TextMesh decodedLines[MORSE_MAX_WORDS];
    int decodedLineCount;
} LevelLoader;
extern LevelLoader levelLoader;
void level_loader_start(void);
void level_loader_update(void);
bool level_loader_next_level(int forceLevel); // false when no level is left
bool level_loader_check(void);
void level_loader_set_decoded_letters_hidden(bool hidden);
void level_loader_draw(const UnityCamera *camera);

// --- Earth (earth.c) ------------------------------------------------------------

void earth_start(void);
void earth_update(void);
void earth_new_sol(void);
void earth_player_is_in_rover(void);
// Rover's AudioSource, the newMessage sound. Earth sets its volume.
extern AudioSource roverNewMessageSound;

// --- RoverScreen (rover_screen.c) ------------------------------------------------

typedef struct {
    GUITexture background;
    GUIText screenGUIText;
    GUIText visibilityIndicator; // Earth/visibiltyIndicator
    int startingLifeSupport;
    int lifeSupport;
    bool on;
} RoverScreen;
extern RoverScreen roverScreen;
void rover_screen_start(void);
void rover_screen_update(void);
void rover_screen_set_on(bool on);
// RoverScreen.printScreen: the screen text. It also sets the visibility
// indicator, as in the source.
const char *rover_screen_print_screen(void);

#endif
