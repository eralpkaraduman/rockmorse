// CamerFocusSwitcher.cs and CutsSceneScaleFixer.cs, on the Game scene's
// Main Camera and its cutscenes child, and the one iTween call the game
// makes. The camera follows the player, or the rover while the player sits
// in it, or shows the whole level while M is held. It also fades the night
// overlay.
#include "game.h"

CameraFocus focus;

// Unity's built-in plane mesh is 10 by 10 units. The night overlay is that
// plane, scaled by 6 and turned to face the camera: 60 by 60 units.
#define UNITY_PLANE_SIZE 10.0f

// The camera as a transform, for the objects that follow it.
static UnityTransform camera_transform(void)
{
    return (UnityTransform){.position = focus.camera.position, .scale = {1, 1, 1}};
}

// original: //public Transform focusTransform;
// (positionTarget, playerIsInRover and overViewForced are in CameraFocus.)

// original: // Use this for initialization
static void Start(void)
{

    focus.positionTarget = player_position();
    focus.defaultCameraSize = focus.camera.orthographicSize;

    // original: //follower.target = focusTransform;
}

// CutsSceneScaleFixer.cs
// original: // Use this for initialization
static void cuts_scene_scale_fixer_start(void)
{
    focus.cutscenesDefaultCameraScale = focus.camera.orthographicSize;
    focus.cutscenesDefaultScale = CUTSCENES_LOCAL_SCALE_X; // this.transform.localScale.x
}

static SpriteRenderer cutscene(int sprite, Rgba color, int sortingOrder, bool enabled)
{
    return (SpriteRenderer){.sprite = sprite, .color = color, .sortingOrder = sortingOrder, .enabled = enabled};
}

void camera_focus_start(void)
{
    focus = (CameraFocus){
        .camera = CAMERA(GAME_CAMERA),
        .playerIsInRover = CAM_FOCUS_PLAYER_IN_ROVER,
        .nightosphere = {
            .transform = {.scale = {NIGHT_OVERLAY_SCALE_X * UNITY_PLANE_SIZE, NIGHT_OVERLAY_SCALE_Z * UNITY_PLANE_SIZE, 1}},
            .color = {NIGHT_OVERLAY_COLOR_R, NIGHT_OVERLAY_COLOR_G, NIGHT_OVERLAY_COLOR_B, NIGHT_OVERLAY_COLOR_A},
            .enabled = NIGHT_OVERLAY_ENABLED,
            .solid = true,
        },
        .couldNotDecode = cutscene(CUT_COULD_NOT_DECODE_SPRITE,
                                   (Rgba){CUT_COULD_NOT_DECODE_COLOR_R, CUT_COULD_NOT_DECODE_COLOR_G,
                                          CUT_COULD_NOT_DECODE_COLOR_B, CUT_COULD_NOT_DECODE_COLOR_A},
                                   CUT_COULD_NOT_DECODE_SORTING_ORDER, CUT_COULD_NOT_DECODE_ENABLED),
        .newMessage = cutscene(CUT_NEW_MESSAGE_SPRITE,
                               (Rgba){CUT_NEW_MESSAGE_COLOR_R, CUT_NEW_MESSAGE_COLOR_G, CUT_NEW_MESSAGE_COLOR_B,
                                      CUT_NEW_MESSAGE_COLOR_A},
                               CUT_NEW_MESSAGE_SORTING_ORDER, CUT_NEW_MESSAGE_ENABLED),
        .couldDecode = cutscene(CUT_COULD_DECODE_SPRITE,
                                (Rgba){CUT_COULD_DECODE_COLOR_R, CUT_COULD_DECODE_COLOR_G, CUT_COULD_DECODE_COLOR_B,
                                       CUT_COULD_DECODE_COLOR_A},
                                CUT_COULD_DECODE_SORTING_ORDER, CUT_COULD_DECODE_ENABLED),
        .tutorial = cutscene(CUT_TUTORIAL_SPRITE,
                             (Rgba){CUT_TUTORIAL_COLOR_R, CUT_TUTORIAL_COLOR_G, CUT_TUTORIAL_COLOR_B,
                                    CUT_TUTORIAL_COLOR_A},
                             CUT_TUTORIAL_SORTING_ORDER, CUT_TUTORIAL_ENABLED),
    };
    Start();
    cuts_scene_scale_fixer_start();
    camera_focus_late_update();
}

// iTween.MoveUpdate(gameObject, position, time), iTween.cs:5205 and :5297.
// Not a lerp: each axis takes one Mathf.SmoothDamp step with a fresh zero
// velocity, a smooth time of time * 0.05, and Time.deltaTime. At deltaTime
// 0 (game paused) the camera stays put.
static void itween_move_update(Vector3 *position, Vector3 target, float time)
{
    time *= ITWEEN_UPDATE_TIME_PERCENTAGE;
    float vx = 0, vy = 0, vz = 0;
    Vector3 p = *position;
    p.x = mathf_smooth_damp(p.x, target.x, &vx, time, INFINITY, Time.deltaTime);
    p.y = mathf_smooth_damp(p.y, target.y, &vy, time, INFINITY, Time.deltaTime);
    p.z = mathf_smooth_damp(p.z, target.z, &vz, time, INFINITY, Time.deltaTime);
    *position = p;
}

static Vector3 clampBounds(Vector3 targetPos)
{

    float vertExtent = focus.camera.orthographicSize;
    float horzExtent = vertExtent * WINDOW_WIDTH / WINDOW_HEIGHT; // Screen.width, Screen.height

    float rightBound = mars.worldBunds.width / 2 - horzExtent;
    float leftBound = -rightBound;

    float topBound = mars.worldBunds.height / 2 - vertExtent;
    float bottomBound = -topBound;

    Vector3 pos = {targetPos.x, targetPos.y, focus.camera.position.z};
    pos.x = mathf_clamp(pos.x, leftBound, rightBound);
    pos.y = mathf_clamp(pos.y, bottomBound, topBound);

    return pos;
}

static float levelFocusCameraSize(void)
{

    float vertExtent = focus.camera.orthographicSize;
    float horzExtent = vertExtent * WINDOW_WIDTH / WINDOW_HEIGHT;
    float p = levelLoader.sentenceSize.x / horzExtent;
    p = fmaxf(p, FOCUS_SWITCHER_OVERVIEW_MIN_RATIO); // Mathf.Max

    return focus.defaultCameraSize * p;
}

void camera_focus_update(void)
{

    float speed = 0;
    float cameraSize = focus.defaultCameraSize;

    bool decodedHidden = true;

    // Input.GetKey("m"). Deliberate change (plan 6.4): Tab, and the
    // gamepad's left shoulder, which sends Tab, also show the map.
    if (input_get_key(KEY_M) || input_get_key(KEY_TAB) || focus.overViewForced) {
        focus.positionTarget = levelLoader.position;
        decodedHidden = false;
        speed = FOCUS_SWITCHER_SPEED_OVERVIEW;
        cameraSize = levelFocusCameraSize();
    } else if (focus.playerIsInRover) {
        focus.positionTarget = rover_focus_target();
        speed = FOCUS_SWITCHER_SPEED_ROVER;
    } else {
        focus.positionTarget = player_position();
        speed = FOCUS_SWITCHER_SPEED_PLAYER;
    }

    level_loader_set_decoded_letters_hidden(decodedHidden);

    // original: // set nightosphere
    Rgba c = focus.nightosphere.color;
    c.a += (mars.darkness * NIGHT_OVERLAY_DARKNESS_FACTOR - c.a) / NIGHT_OVERLAY_EASE_DIVISOR;
    focus.nightosphere.color = c;

    focus.positionTarget = clampBounds(focus.positionTarget);


    // speed is never negative, so the else branch never runs.
    if (speed >= 0) {
        itween_move_update(&focus.camera.position, focus.positionTarget, speed);
        focus.camera.orthographicSize += (cameraSize - focus.camera.orthographicSize) / FOCUS_SWITCHER_SIZE_EASE_DIVISOR;
    } else {
        focus.camera.position = focus.positionTarget;
        focus.camera.orthographicSize = cameraSize;
    }

}

void camera_focus_force_overview(bool forced)
{
    focus.overViewForced = forced;
}

// CutsSceneScaleFixer.LateUpdate, then the camera's children follow it:
// they are drawn where the camera is now.
// original: // Update is called once per frame
void camera_focus_late_update(void)
{

    float newScale = focus.cutscenesDefaultScale * focus.camera.orthographicSize / focus.cutscenesDefaultCameraScale;
    Vector3 localScale = {newScale, newScale, newScale};

    UnityTransform cam = camera_transform();
    transform_follow(&focus.cutscenes, &cam, (Vector3){CUTSCENES_LOCAL_POS_X, CUTSCENES_LOCAL_POS_Y, CUTSCENES_LOCAL_POS_Z},
                     localScale);
#define FOLLOW_CUTSCENES(r, p) transform_follow(&(r).transform, &focus.cutscenes, \
        (Vector3){p##_LOCAL_POS_X, p##_LOCAL_POS_Y, p##_LOCAL_POS_Z}, \
        (Vector3){p##_LOCAL_SCALE_X, p##_LOCAL_SCALE_Y, p##_LOCAL_SCALE_Z})
    FOLLOW_CUTSCENES(focus.couldNotDecode, CUT_COULD_NOT_DECODE);
    FOLLOW_CUTSCENES(focus.newMessage, CUT_NEW_MESSAGE);
    FOLLOW_CUTSCENES(focus.couldDecode, CUT_COULD_DECODE);
    FOLLOW_CUTSCENES(focus.tutorial, CUT_TUTORIAL);
#undef FOLLOW_CUTSCENES
    transform_follow(&focus.nightosphere.transform, &cam,
                     (Vector3){NIGHT_OVERLAY_LOCAL_POS_X, NIGHT_OVERLAY_LOCAL_POS_Y, NIGHT_OVERLAY_LOCAL_POS_Z},
                     focus.nightosphere.transform.scale);
}

void camera_focus_sprites(SpriteList *list)
{
    sprite_list_add(list, &focus.nightosphere);
}

// The cutscene images, z -29 like the decoded letters. game.c draws them
// after the letters, so an image covers them. Unity's order for this tie
// is not known.
void camera_focus_cutscene_sprites(SpriteList *list)
{
    sprite_list_add(list, &focus.couldNotDecode);
    sprite_list_add(list, &focus.newMessage);
    sprite_list_add(list, &focus.couldDecode);
    sprite_list_add(list, &focus.tutorial);
}
