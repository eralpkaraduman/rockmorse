// unity: see unity.h. Frame loop, time, input, drawing, text and audio.
#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The engine draws the Joystix font itself, with the stb_truetype copy
// inside raylib. raylib compiles it as static functions, so this copy does
// not clash with it.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" // the parts of stb this file does not call
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

UnityTime Time;

// --- Scenes and the frame loop ---------------------------------------------

static const Scene *const *scenes;
static int activeScene = -1;
static int pendingScene = -1;

// Game time in double, as Unity keeps it. Time.time is the float view.
static double timeNow;
static double fixedTimeNow;

static float axisValue[AXIS_COUNT];

static void audio_scene_unload(void);
static void audio_sources_update(void);
static void axes_update(float dt);

const char *scene_name(SceneId id)
{
    return scenes && id >= 0 && id < SCENE_COUNT ? scenes[id]->name : "";
}

int scene_find(const char *name)
{
    for (int i = 0; scenes && i < SCENE_COUNT; i++) {
        if (scenes[i]->name && strcmp(scenes[i]->name, name) == 0) return i;
    }
    return -1;
}

void scene_load(SceneId id)
{
    if (id >= 0 && id < SCENE_COUNT) pendingScene = id;
}

void scene_load_name(const char *name)
{
    int id = scene_find(name);
    if (id < 0) TraceLog(LOG_WARNING, "LoadScene: no scene named %s", name);
    else scene_load((SceneId)id);
}

SceneId scene_active(void)
{
    return (SceneId)activeScene;
}

void unity_init(const Scene *const *table, SceneId first)
{
    scenes = table;
    activeScene = -1;
    pendingScene = first;
    timeNow = 0;
    fixedTimeNow = 0;
    Time = (UnityTime){
        .fixedDeltaTime = FIXED_TIMESTEP,
        .timeScale = TIME_SCALE_START,
    };
    memset(axisValue, 0, sizeof axisValue);
}

#define CALL(hook) do { if (scenes[activeScene]->hook) scenes[activeScene]->hook(); } while (0)

// One frame in Unity's order. A scene loaded during the previous frame
// starts first, so its Start sees this frame's time. Fixed updates catch up
// with game time, so they stop at timeScale 0 while Update keeps running.
void unity_frame(void)
{
    Time.unscaledDeltaTime = UNITY_FRAME_TIME;
    float dt = UNITY_FRAME_TIME < MAX_ALLOWED_TIMESTEP ? UNITY_FRAME_TIME : MAX_ALLOWED_TIMESTEP;
    Time.deltaTime = dt * Time.timeScale;
    timeNow += Time.deltaTime;
    Time.time = (float)timeNow;
    // Input axes move with scaled time, so a pause freezes them. Measured
    // on the 2016 WebGL build: an arrow held through the paused tutorial
    // ramps up from 0 over 20 frames after Space, and an arrow released
    // during a pause still decays over 20 frames after it.
    axes_update(Time.deltaTime);

    if (pendingScene >= 0) {
        audio_scene_unload();
        activeScene = pendingScene;
        pendingScene = -1;
        CALL(start);
    }
    if (activeScene < 0) return;

    while (fixedTimeNow + Time.fixedDeltaTime <= timeNow) {
        fixedTimeNow += Time.fixedDeltaTime;
        float frameDelta = Time.deltaTime;
        Time.time = (float)fixedTimeNow;
        Time.deltaTime = Time.fixedDeltaTime;
        CALL(fixed_update);
        CALL(physics);
        Time.time = (float)timeNow;
        Time.deltaTime = frameDelta;
    }
    CALL(update);
    CALL(late_update);
    CALL(end_of_frame);
    audio_sources_update();
}

void unity_draw(void)
{
    if (activeScene >= 0) CALL(draw);
}

// --- Input -----------------------------------------------------------------
// Key latching, touch keys and the gamepad poll are copied from Kurban
// Bayrami's flx.c. The gamepad map follows plan section 6.4.

#define KEY_SLOTS 512
#define MOUSE_SLOTS 3

static bool pressed[KEY_SLOTS];
static bool released[KEY_SLOTS];
static bool touchDown[KEY_SLOTS];
static bool padDown[KEY_SLOTS];
static bool mousePressed[MOUSE_SLOTS];
static bool mouseReleased[MOUSE_SLOTS];
static Vector2 mouseOffset;
static float mouseScale = 1;

// Touch events arrive between frames, so they latch presses and releases
// just like the keyboard does in input_poll.
#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void flx_touch_key(int key, bool down)
{
    if (key < 0 || key >= KEY_SLOTS || touchDown[key] == down) return;
    touchDown[key] = down;
    if (down) pressed[key] = true;
    else released[key] = true;
}

bool input_get_key(int key)
{
    if (key < 0 || key >= KEY_SLOTS) return false;
    return IsKeyDown(key) || touchDown[key] || padDown[key];
}

// Any connected gamepad stands in for keys. The D-pad is the arrows, A is
// Space (pull, dismiss, play), the left shoulder is Tab (map) and Start is
// H (tutorial). The left stick is read as an axis, see input_get_axis.
static void gamepad_poll(void)
{
    static const int keys[] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_SPACE, KEY_TAB, KEY_H};
    enum { COUNT = sizeof keys / sizeof keys[0] };
    bool down[COUNT] = {0};
    for (int g = 0; g < 4; g++) {
        if (!IsGamepadAvailable(g)) continue;
        down[0] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
        down[1] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
        down[2] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_UP);
        down[3] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        down[4] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        down[5] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
        down[6] |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    }
    for (int i = 0; i < COUNT; i++) {
        if (padDown[keys[i]] == down[i]) continue;
        padDown[keys[i]] = down[i];
        if (down[i]) pressed[keys[i]] = true;
        else released[keys[i]] = true;
    }
}

void input_poll(void)
{
    gamepad_poll();
    for (int key = 0; key < KEY_SLOTS; key++) {
        if (IsKeyPressed(key)) pressed[key] = true;
        if (IsKeyReleased(key)) released[key] = true;
    }
    for (int b = 0; b < MOUSE_SLOTS; b++) {
        if (IsMouseButtonPressed(b)) mousePressed[b] = true;
        if (IsMouseButtonReleased(b)) mouseReleased[b] = true;
    }
}

void input_clear(void)
{
    memset(pressed, 0, sizeof pressed);
    memset(released, 0, sizeof released);
    memset(mousePressed, 0, sizeof mousePressed);
    memset(mouseReleased, 0, sizeof mouseReleased);
}

bool input_get_key_down(int key)
{
    return key >= 0 && key < KEY_SLOTS && pressed[key];
}

bool input_get_key_up(int key)
{
    return key >= 0 && key < KEY_SLOTS && released[key];
}

bool input_get_mouse_button(int button)
{
    return button >= 0 && button < MOUSE_SLOTS && IsMouseButtonDown(button);
}

bool input_get_mouse_button_down(int button)
{
    return button >= 0 && button < MOUSE_SLOTS && mousePressed[button];
}

bool input_get_mouse_button_up(int button)
{
    return button >= 0 && button < MOUSE_SLOTS && mouseReleased[button];
}

void input_set_mouse_mapping(Vector2 offset, float scale)
{
    mouseOffset = offset;
    mouseScale = scale;
}

// Game pixels, origin top left.
static Vector2 mouse_game_pixels(void)
{
    Vector2 m = GetMousePosition();
    return (Vector2){(m.x - mouseOffset.x) * mouseScale, (m.y - mouseOffset.y) * mouseScale};
}

Vector2 input_mouse_position(void)
{
    Vector2 m = mouse_game_pixels();
    return (Vector2){m.x, WINDOW_HEIGHT - m.y};
}

static float move_towards(float current, float target, float maxDelta)
{
    if (fabsf(target - current) <= maxDelta) return target;
    return current + (target > current ? maxDelta : -maxDelta);
}

// Unity's key axis: the value moves toward the held direction by
// sensitivity per second, and back to 0 by gravity per second when no key
// (or both) is held. With snap, pressing the opposite direction first
// jumps the value to 0.
// ponytail: Unity's rule as documented, measured in step 3 of the plan.
float axis_smooth(float value, bool positive, bool negative, float dt,
                  float sensitivity, float gravity, bool snap)
{
    float target = (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    if (target == 0) return move_towards(value, 0, gravity * dt);
    if (snap && value * target < 0) value = 0;
    return move_towards(value, target, sensitivity * dt);
}

float axis_dead_zone(float value, float dead)
{
    return fabsf(value) < dead ? 0 : value;
}

static void axes_update(float dt)
{
    axisValue[AXIS_HORIZONTAL] = axis_smooth(axisValue[AXIS_HORIZONTAL],
        input_get_key(KEY_RIGHT) || input_get_key(KEY_D),
        input_get_key(KEY_LEFT) || input_get_key(KEY_A), dt,
        INPUT_KEY_HORIZONTAL_SENSITIVITY, INPUT_KEY_HORIZONTAL_GRAVITY, INPUT_KEY_HORIZONTAL_SNAP);
    axisValue[AXIS_VERTICAL] = axis_smooth(axisValue[AXIS_VERTICAL],
        input_get_key(KEY_UP) || input_get_key(KEY_W),
        input_get_key(KEY_DOWN) || input_get_key(KEY_S), dt,
        INPUT_KEY_VERTICAL_SENSITIVITY, INPUT_KEY_VERTICAL_GRAVITY, INPUT_KEY_VERTICAL_SNAP);
}

// The joystick axes of settings/InputManager.json are m_Axes[9]
// (Horizontal) and m_Axes[10] (Vertical, inverted). Both have dead zone
// 0.19 and sensitivity 1.

static float stick(int axis)
{
    float best = 0;
    for (int g = 0; g < 4; g++) {
        if (!IsGamepadAvailable(g)) continue;
        float v = GetGamepadAxisMovement(g, axis == AXIS_HORIZONTAL ? GAMEPAD_AXIS_LEFT_X : GAMEPAD_AXIS_LEFT_Y);
        // raylib's stick y grows downward, like Unity's raw joystick axis,
        // which the Vertical axis inverts.
        if (axis == AXIS_VERTICAL && INPUT_STICK_VERTICAL_INVERT) v = -v;
        v = axis_dead_zone(v, axis == AXIS_HORIZONTAL ? INPUT_STICK_HORIZONTAL_DEAD : INPUT_STICK_VERTICAL_DEAD);
        if (fabsf(v) > fabsf(best)) best = v;
    }
    return best;
}

float input_get_axis(Axis axis)
{
    float dead = axis == AXIS_HORIZONTAL ? INPUT_KEY_HORIZONTAL_DEAD : INPUT_KEY_VERTICAL_DEAD;
    float keys = axis_dead_zone(axisValue[axis], dead);
    float pad = stick(axis);
    return fabsf(pad) > fabsf(keys) ? pad : keys;
}

// --- Maths -------------------------------------------------------------------

float mathf_round(float f)
{
    return rintf(f); // the default rounding mode rounds halves to even
}

float mathf_clamp(float value, float min, float max)
{
    if (value < min) value = min;
    else if (value > max) value = max;
    return value;
}

float mathf_lerp(float a, float b, float t)
{
    return a + (b - a) * mathf_clamp(t, 0, 1); // Clamp01
}

// Copied from the decompiled UnityEngine.dll (INVENTORY.md section 2.21),
// in float and in the same order of operations.
float mathf_smooth_damp(float current, float target, float *currentVelocity,
                        float smoothTime, float maxSpeed, float deltaTime)
{
    smoothTime = 0.0001f > smoothTime ? 0.0001f : smoothTime; // Mathf.Max(0.0001f, smoothTime)
    float num = 2.0f / smoothTime;
    float num2 = num * deltaTime;
    float num3 = 1.0f / (1.0f + num2 + 0.48f * num2 * num2 + 0.235f * num2 * num2 * num2);
    float value = current - target;
    float num4 = target;
    float num5 = maxSpeed * smoothTime;
    if (value < -num5) value = -num5; // Mathf.Clamp(value, -num5, num5)
    else if (value > num5) value = num5;
    target = current - value;
    float num6 = (*currentVelocity + num * value) * deltaTime;
    *currentVelocity = (*currentVelocity - num * num6) * num3;
    float num7 = target + (value + num6) * num3;
    if ((num4 - current > 0.0f) == (num7 > num4)) {
        num7 = num4;
        *currentVelocity = (num7 - num4) / deltaTime;
    }
    return num7;
}

int random_range_int(int min, int max)
{
    if (max <= min) return min;
    return GetRandomValue(min, max - 1);
}

#define RANDOM_STEPS 1000000

float random_range_float(float min, float max)
{
    return min + (max - min) * ((float)GetRandomValue(0, RANDOM_STEPS) / RANDOM_STEPS);
}

// --- Transforms ------------------------------------------------------------

void transform_follow(UnityTransform *child, const UnityTransform *parent, Vector3 localPosition, Vector3 localScale)
{
    float x = localPosition.x * parent->scale.x;
    child->position = (Vector3){
        parent->position.x + (parent->flipped ? -x : x),
        parent->position.y + localPosition.y * parent->scale.y,
        parent->position.z + localPosition.z * parent->scale.z,
    };
    child->scale = (Vector3){parent->scale.x * localScale.x, parent->scale.y * localScale.y, parent->scale.z * localScale.z};
    child->flipped = parent->flipped;
}

// --- Drawing -----------------------------------------------------------------

static Color to_color(Rgba c)
{
    float v[4] = {c.r, c.g, c.b, c.a};
    unsigned char out[4];
    for (int i = 0; i < 4; i++) {
        float f = v[i] < 0 ? 0 : (v[i] > 1 ? 1 : v[i]);
        out[i] = (unsigned char)(f * 255.0f + 0.5f);
    }
    return (Color){out[0], out[1], out[2], out[3]};
}

static Texture2D textures[IMG_COUNT];

// Images load on first use, with the filter of their import settings.
static Texture2D image_texture(int image)
{
    if (textures[image].id == 0) {
        textures[image] = LoadTexture(TextFormat("assets/%s", IMAGES[image].path));
        SetTextureFilter(textures[image], IMAGES[image].filter_mode == 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }
    return textures[image];
}

void camera_clear(const UnityCamera *camera)
{
    Rgba bg = camera->background;
    bg.a = 1; // the alpha of a solid colour clear never shows
    ClearBackground(to_color(bg));
}

static float pixels_per_unit(const UnityCamera *camera)
{
    return WINDOW_HEIGHT / (2.0f * camera->orthographicSize);
}

Vector2 camera_world_to_screen(const UnityCamera *camera, Vector3 world)
{
    float ppu = pixels_per_unit(camera);
    return (Vector2){
        WINDOW_WIDTH / 2.0f + (world.x - camera->position.x) * ppu,
        WINDOW_HEIGHT / 2.0f - (world.y - camera->position.y) * ppu,
    };
}

static void draw_sprite(const UnityCamera *camera, const SpriteRenderer *r)
{
    float ppu = pixels_per_unit(camera);
    if (r->solid) {
        float w = r->transform.scale.x * ppu, h = r->transform.scale.y * ppu;
        Vector2 at = camera_world_to_screen(camera, r->transform.position);
        DrawRectangleRec((Rectangle){at.x - w / 2, at.y - h / 2, w, h}, to_color(r->color));
        return;
    }
    const SpriteInfo *s = &SPRITES[r->sprite];
    float w = s->w / s->pixels_per_unit * r->transform.scale.x * ppu;
    float h = s->h / s->pixels_per_unit * r->transform.scale.y * ppu;
    Vector2 at = camera_world_to_screen(camera, r->transform.position);
    Rectangle source = {(float)s->x, (float)s->y_from_top, (float)s->w, (float)s->h};
    // The pivot is from the bottom left in Unity, the origin from the top left here.
    Vector2 origin = {s->pivot_x * w, (1 - s->pivot_y) * h};
    if (r->transform.flipped) {
        source.width = -source.width;
        origin.x = w - origin.x;
    }
    DrawTexturePro(image_texture(s->image), source, (Rectangle){at.x, at.y, w, h}, origin,
                   -r->transform.angle, to_color(r->color));
}

typedef struct { const SpriteRenderer *r; int index; } SortEntry;

static int compare_draw_order(const void *pa, const void *pb)
{
    const SortEntry *a = pa, *b = pb;
    if (a->r->sortingOrder != b->r->sortingOrder) return a->r->sortingOrder < b->r->sortingOrder ? -1 : 1;
    float za = a->r->transform.position.z, zb = b->r->transform.position.z;
    if (za != zb) return za > zb ? -1 : 1;
    return a->index - b->index;
}

#define MAX_SPRITES 16384

void draw_sprites(const UnityCamera *camera, SpriteRenderer *const *sprites, int count)
{
    static SortEntry order[MAX_SPRITES];
    int n = 0;
    for (int i = 0; i < count && n < MAX_SPRITES; i++) {
        if (sprites[i]->enabled && (sprites[i]->sprite >= 0 || sprites[i]->solid)) order[n++] = (SortEntry){sprites[i], i};
    }
    qsort(order, n, sizeof order[0], compare_draw_order);
    for (int i = 0; i < n; i++) draw_sprite(camera, order[i].r);
}

// --- Sprite animation ----------------------------------------------------

Animator animator_new(void)
{
    return (Animator){.state = ANIM_DEFAULT_STATE};
}

void animator_play(Animator *a, const char *state)
{
    for (int i = 0; i < ANIM_STATE_COUNT; i++) {
        if (strcmp(ANIM_STATES[i].name, state) != 0) continue;
        if (i != a->state) *a = (Animator){.state = i};
        return;
    }
    TraceLog(LOG_WARNING, "Animator.Play: no state %s", state);
}

void animator_update(Animator *a, float deltaTime)
{
    a->time += deltaTime * ANIM_STATES[a->state].speed;
}

// A sprite curve steps: each key holds until the next. All clips loop.
int animator_sprite(const Animator *a)
{
    const Clip *c = &CLIPS[ANIM_STATES[a->state].clip];
    float t = c->loop ? fmodf(a->time, c->length) : fminf(a->time, c->length);
    int sprite = c->keys[0].sprite;
    for (int k = 0; k < c->key_count && c->keys[k].time <= t; k++) sprite = c->keys[k].sprite;
    return sprite;
}

// --- Text ------------------------------------------------------------------
// Unity rasterises its dynamic font from the TTF at each text's size, with
// one em equal to the font size in pixels. Measured on the 2016 WebGL
// build's title screen (sizes 12, 17, 40 and 50):
// - glyphs are unhinted and antialiased, drawn at whole pixel origins;
// - the advance is the font's 828 units at that size, rounded to whole
//   pixels (33 px at size 40, 10 at 12, 14 at 17, 41 at 50);
// - a line box is ascent minus descent high (hhea 1001 and -199, so 1.2
//   times the size), and the baseline sits ascent below the box top;
// - the anchor places the box, then line origins and baselines round to
//   the nearest pixel (ROCKMORSE: x 181.5 became 182).
// The mouse hits the box between the rounded baselines' line tops and
// bottoms, shrunk to whole pixels (PLAY: rows 322 to 380, columns 248 to 411).

#define FIRST_CHAR 32
#define LAST_CHAR 126
#define MAX_FONT_SIZES 16

typedef struct {
    int size;
    Texture2D atlas;
    Rectangle rect[LAST_CHAR - FIRST_CHAR + 1];
    Vector2 offset[LAST_CHAR - FIRST_CHAR + 1]; // bitmap top left from the pen at the baseline
    int advance;
    float ascent, descent; // pixels, descent below the baseline is positive
} FontSize;

static unsigned char *fontData;
static stbtt_fontinfo fontInfo;
static FontSize fontSizes[MAX_FONT_SIZES];
static int fontSizeCount;

static bool font_open(void)
{
    if (fontData) return true;
    int bytes = 0;
    fontData = LoadFileData(TextFormat("assets/%s", FONTS[FONT_JOYSTIX_MONOSPACE].path), &bytes);
    if (!fontData || !stbtt_InitFont(&fontInfo, fontData, 0)) {
        TraceLog(LOG_ERROR, "Could not load the Joystix font");
        UnloadFileData(fontData);
        fontData = NULL;
        return false;
    }
    return true;
}

// The metrics of one size. The atlas texture is built only when drawing.
static FontSize *font_size(int size, bool needAtlas)
{
    FontSize *f = NULL;
    for (int i = 0; i < fontSizeCount; i++) {
        if (fontSizes[i].size == size) f = &fontSizes[i];
    }
    if (!f) {
        if (fontSizeCount == MAX_FONT_SIZES || !font_open()) return NULL;
        f = &fontSizes[fontSizeCount++];
        *f = (FontSize){.size = size};
        float scale = stbtt_ScaleForMappingEmToPixels(&fontInfo, (float)size);
        int ascent, descent, lineGap, advance, lsb;
        stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
        stbtt_GetCodepointHMetrics(&fontInfo, 'A', &advance, &lsb);
        f->ascent = ascent * scale;
        f->descent = -descent * scale;
        f->advance = (int)floorf(advance * scale + 0.5f);
    }
    if (needAtlas && f->atlas.id == 0) {
        float scale = stbtt_ScaleForMappingEmToPixels(&fontInfo, (float)size);
        // One cell per glyph, in rows of 16. A glyph can stick out of its
        // em box a little, so cells get a margin.
        int cell = (int)ceilf(size * 1.25f) + 2;
        Image atlas = GenImageColor(cell * 16, cell * 6, BLANK);
        for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char *bitmap = stbtt_GetCodepointBitmap(&fontInfo, 0, scale, c, &w, &h, &xoff, &yoff);
            int i = c - FIRST_CHAR;
            int cx = (i % 16) * cell, cy = (i / 16) * cell;
            if (w > cell) w = cell;
            if (h > cell) h = cell;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    ImageDrawPixel(&atlas, cx + x, cy + y, (Color){255, 255, 255, bitmap[y * w + x]});
                }
            }
            stbtt_FreeBitmap(bitmap, NULL);
            f->rect[i] = (Rectangle){(float)cx, (float)cy, (float)w, (float)h};
            f->offset[i] = (Vector2){(float)xoff, (float)yoff};
        }
        f->atlas = LoadTextureFromImage(atlas);
        UnloadImage(atlas);
    }
    return f;
}

static float round_half_up(float v)
{
    return floorf(v + 0.5f);
}

typedef struct {
    float x, y, w, h;  // the text block, top left origin, before rounding
    int lines;
    float lineHeight;
} TextBlock;

static int line_length(const char *s)
{
    const char *end = strchr(s, '\n');
    return end ? (int)(end - s) : (int)strlen(s);
}

static TextBlock text_block(const char *text, const FontSize *f, float lineSpacing, int anchor, Vector2 at)
{
    // Lines sit a whole number of pixels apart, rounded. Measured on the
    // 2016 WebGL build: the rover screen's 13 pt lines are 16 px apart
    // (15.6 unrounded), the visibility indicator's 11 pt lines 13 px (13.2).
    TextBlock b = {.lineHeight = round_half_up((f->ascent + f->descent) * lineSpacing)};
    int widest = 0;
    for (const char *s = text;; ) {
        int n = line_length(s);
        if (n > widest) widest = n;
        b.lines++;
        if (!s[n]) break;
        s += n + 1;
    }
    b.w = (float)(widest * f->advance);
    b.h = b.lines * b.lineHeight;
    int col = anchor % 3, row = anchor / 3;
    b.x = at.x - (col == 1 ? b.w / 2 : col == 2 ? b.w : 0);
    b.y = at.y - (row == 1 ? b.h / 2 : row == 2 ? b.h : 0);
    return b;
}

// Origin of line i: x of its pen at the start and y of its baseline.
static Vector2 line_origin(const TextBlock *b, const FontSize *f, int alignment, int length, int i)
{
    float lw = (float)(length * f->advance);
    float x = b->x + (alignment == 1 ? (b->w - lw) / 2 : alignment == 2 ? b->w - lw : 0);
    float y = b->y + f->ascent + i * b->lineHeight;
    return (Vector2){round_half_up(x), round_half_up(y)};
}

// Anchor point of a GUIText in game pixels, top left origin.
static Vector2 gui_text_anchor(const GUIText *t)
{
    return (Vector2){t->position.x * WINDOW_WIDTH + t->pixelOffset.x,
                     WINDOW_HEIGHT - (t->position.y * WINDOW_HEIGHT + t->pixelOffset.y)};
}

void gui_text_set(GUIText *t, const char *text)
{
    snprintf(t->text, sizeof t->text, "%s", text);
}

void gui_text_draw(const GUIText *t)
{
    if (!t->enabled || !t->text[0]) return;
    const FontSize *f = font_size(t->fontSize, true);
    if (!f) return;
    TextBlock b = text_block(t->text, f, t->lineSpacing, t->anchor, gui_text_anchor(t));
    Color tint = to_color(t->color);
    const char *s = t->text;
    for (int line = 0; line < b.lines; line++) {
        int n = line_length(s);
        Vector2 pen = line_origin(&b, f, t->alignment, n, line);
        for (int k = 0; k < n; k++, pen.x += f->advance) {
            int c = (unsigned char)s[k];
            if (c <= FIRST_CHAR || c > LAST_CHAR) continue;
            int i = c - FIRST_CHAR;
            DrawTextureRec(f->atlas, f->rect[i], (Vector2){pen.x + f->offset[i].x, pen.y + f->offset[i].y}, tint);
        }
        s += n + (s[n] ? 1 : 0);
    }
}

Rectangle gui_text_screen_rect(const GUIText *t)
{
    const FontSize *f = font_size(t->fontSize, false);
    if (!f) return (Rectangle){0};
    TextBlock b = text_block(t->text, f, t->lineSpacing, t->anchor, gui_text_anchor(t));
    Vector2 first = line_origin(&b, f, ANCHOR_UPPER_LEFT, 0, 0);
    Vector2 last = line_origin(&b, f, ANCHOR_UPPER_LEFT, 0, b.lines - 1);
    float top = ceilf(first.y - f->ascent);
    float bottom = floorf(last.y + f->descent);
    return (Rectangle){first.x, top, b.w, bottom - top};
}

MouseEvents mouse_target_update(MouseTarget *target, bool over, bool buttonDown, bool buttonUp)
{
    MouseEvents e = {0};
    if (over != target->over) {
        if (over) e.enter = true;
        else e.exit = true;
        target->over = over;
    }
    if (buttonDown && over) target->pressed = true;
    if (buttonUp && target->pressed) {
        target->pressed = false;
        e.up = true;
    }
    return e;
}

MouseEvents gui_text_mouse_events(const GUIText *t, MouseTarget *target)
{
    Rectangle r = gui_text_screen_rect(t);
    Vector2 m = mouse_game_pixels();
    // Whole pixels: a pixel row or column is in when it lies in the box.
    float x = floorf(m.x), y = floorf(m.y);
    bool over = t->enabled && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
    return mouse_target_update(target, over, input_get_mouse_button_down(MOUSE_BUTTON_LEFT),
                               input_get_mouse_button_up(MOUSE_BUTTON_LEFT));
}

void text_mesh_draw(const UnityCamera *camera, const TextMesh *t)
{
    if (!t->enabled || !t->text[0]) return;
    const FontSize *f = font_size(t->fontSize, true);
    if (!f) return;
    // Font pixels to screen pixels.
    float k = t->characterSize * t->pixelScale * t->transform.scale.y * pixels_per_unit(camera);
    TextBlock b = text_block(t->text, f, 1, t->anchor, (Vector2){0, 0});
    Vector2 at = camera_world_to_screen(camera, t->transform.position);
    Color tint = to_color(t->color);
    const char *s = t->text;
    for (int line = 0; line < b.lines; line++) {
        int n = line_length(s);
        float lw = (float)(n * f->advance);
        float x = b.x + (t->alignment == 1 ? (b.w - lw) / 2 : t->alignment == 2 ? b.w - lw : 0);
        float y = b.y + f->ascent + line * b.lineHeight;
        for (int c = 0; c < n; c++, x += f->advance) {
            int ch = (unsigned char)s[c];
            if (ch <= FIRST_CHAR || ch > LAST_CHAR) continue;
            int i = ch - FIRST_CHAR;
            Rectangle dest = {at.x + (x + f->offset[i].x) * k, at.y + (y + f->offset[i].y) * k,
                              f->rect[i].width * k, f->rect[i].height * k};
            DrawTexturePro(f->atlas, f->rect[i], dest, (Vector2){0, 0}, 0, tint);
        }
        s += n + (s[n] ? 1 : 0);
    }
}

// The screen rectangle is the transform's viewport position, less half the
// scaled screen, plus the pixel inset. Unity documents this for scale 0.
// ponytail: the rover screen's scale 1 with inset width -220 is unverified,
// step 6 of the plan checks it on the reference.
void gui_texture_draw(const GUITexture *t)
{
    if (!t->enabled) return;
    float w = t->scale.x * WINDOW_WIDTH + t->pixelInset.width;
    float h = t->scale.y * WINDOW_HEIGHT + t->pixelInset.height;
    float x = t->position.x * WINDOW_WIDTH - t->scale.x * WINDOW_WIDTH / 2 + t->pixelInset.x;
    float y = t->position.y * WINDOW_HEIGHT - t->scale.y * WINDOW_HEIGHT / 2 + t->pixelInset.y;
    Texture2D tex = image_texture(t->image);
    // A GUITexture colour of 0.5 draws the texture unchanged.
    Rgba c = {t->color.r * 2, t->color.g * 2, t->color.b * 2, t->color.a * 2};
    DrawTexturePro(tex, (Rectangle){0, 0, (float)tex.width, (float)tex.height},
                   (Rectangle){x, WINDOW_HEIGHT - y - h, w, h}, (Vector2){0, 0}, 0, to_color(c));
}

// --- Animation curves --------------------------------------------------------

#define KEY_FIELDS 5 // inSlope, outSlope, tangentMode, time, value
#define KEY_IN_SLOPE(k) (k)[0]
#define KEY_OUT_SLOPE(k) (k)[1]
#define KEY_TIME(k) (k)[3]
#define KEY_VALUE(k) (k)[4]

float curve_evaluate(const float *keys, int count, float time)
{
    const float *first = keys, *last = keys + (count - 1) * KEY_FIELDS;
    if (time <= KEY_TIME(first)) return KEY_VALUE(first);
    if (time >= KEY_TIME(last)) return KEY_VALUE(last);
    const float *a = first;
    while (KEY_TIME(a + KEY_FIELDS) < time) a += KEY_FIELDS;
    const float *b = a + KEY_FIELDS;
    float dt = KEY_TIME(b) - KEY_TIME(a);
    float t = (time - KEY_TIME(a)) / dt;
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * KEY_VALUE(a) + (t3 - 2 * t2 + t) * dt * KEY_OUT_SLOPE(a) +
           (-2 * t3 + 3 * t2) * KEY_VALUE(b) + (t3 - t2) * dt * KEY_IN_SLOPE(b);
}

// --- Audio -------------------------------------------------------------------
// The 3D sound follows the 2016 WebGL build, the only reference that could be
// measured here (the Mac build cannot be heard headless). Unity's WebGL
// player works out each source's volume itself and puts it on a GainNode.
// Its PannerNode has rolloffFactor 0, so it only pans, with Chrome's
// default HRTF model. The pitch with the Doppler shift goes on the
// buffer's playbackRate. Logged on that build while walking past the rover
// and dragging rocks:
// - volume = AudioSource.volume * rolloff, with the 3D distance (z
//   counts: the listener is at z -1, rocks and shatters at -3), and the
//   custom curves read at distance / maxDistance;
// - the pitch moves with the frame's velocity of listener and source, so
//   it is back to exactly 1 on the frames with no physics step;
// - the listener turns as the code turns it (plan 6.3, item 5);
// - no ConvolverNode or other effect exists, so the reverb zones do
//   nothing in that build. The port leaves reverb out (port/README.md).
// Spread has no WebGL counterpart either, so the port leaves it out.
// raylib cannot do HRTF; the port pans with the equal-power law that
// WebAudio defines for the same azimuth.

#define MAX_LIVE_SOURCES 256

// raylib's pan law gives each channel 0.5 * x * (3 - x * x), with x = 0.5
// for both channels at pan 0 (raudio.c, MixAudioFrames).
#define RAYLIB_PAN_CENTRE_LEVEL (0.5f * 0.5f * (3 - 0.5f * 0.5f))

static Sound clips[SND_COUNT];
static AudioSource *liveSources[MAX_LIVE_SOURCES];
static int liveSourceCount;
static float listenerVolume = 1;
static Vector3 listenerPosition, listenerVelocity;
static bool listenerTurned, listenerPlaced;

static const char *clip_path(int clip)
{
    return TextFormat("assets/%s", SOUNDS[clip].path);
}

float audio_rolloff(const AudioSource *s, float distance)
{
    switch (s->rolloffMode) {
    case ROLLOFF_LOGARITHMIC:
        // FMOD's inverse rolloff, which stops falling at the max distance.
        return s->minDistance / mathf_clamp(distance, s->minDistance, s->maxDistance);
    case ROLLOFF_LINEAR:
        return mathf_clamp((s->maxDistance - distance) / (s->maxDistance - s->minDistance), 0, 1);
    default:
        return s->rolloffCurve ? curve_evaluate(s->rolloffCurve, 2, distance / s->maxDistance) : 1;
    }
}

static float dot3(Vector3 a, Vector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

AudioMix audio_mix(const AudioSource *s, Vector3 listener, bool turned, Vector3 lv, Vector3 sv)
{
    Vector3 to = {s->position.x - listener.x, s->position.y - listener.y, s->position.z - listener.z};
    float distance = sqrtf(dot3(to, to));
    // WebAudio's azimuth: the listener's up is +y, so only x and z count.
    // Equal power folds the back onto the front, so the sine of the
    // azimuth is the part of the direction along the listener's right ear.
    float flat = sqrtf(to.x * to.x + to.z * to.z);
    float side = flat > 0 ? (turned ? -to.x : to.x) / flat : 0;
    float pan3d = asinf(mathf_clamp(side, -1, 1)) / (PI / 2);
    float b = s->spatialBlend;
    AudioMix m = {
        // A 2D source plays unpanned, at raylib's centre level. A 3D one at
        // full pan should match it, so its level is scaled by that too.
        .volume = s->volume * ((1 - b) + b * RAYLIB_PAN_CENTRE_LEVEL * audio_rolloff(s, distance)),
        .pan = b * pan3d,
        .pitch = s->pitch,
    };
    if (b > 0 && distance > 0) {
        // Doppler: velocities along the line from the listener to the source.
        float k = s->dopplerLevel * AUDIO_DOPPLER_FACTOR / distance;
        m.pitch *= (AUDIO_SPEED_OF_SOUND + k * dot3(lv, to)) / (AUDIO_SPEED_OF_SOUND + k * dot3(sv, to));
    }
    return m;
}

// Loads a source's sound on its first play, so the Game scene's hundred
// rocks with a drag sound each do not open a hundred streams.
static bool audio_source_load(AudioSource *s)
{
    if (s->loaded) return true;
    if (!IsAudioDeviceReady() || liveSourceCount == MAX_LIVE_SOURCES) return false;
    if (s->loop) {
        s->music = LoadMusicStream(clip_path(s->clip));
        s->music.looping = true;
    } else {
        if (clips[s->clip].frameCount == 0) clips[s->clip] = LoadSound(clip_path(s->clip));
        s->sound = LoadSoundAlias(clips[s->clip]);
    }
    s->loaded = true;
    s->hasLastPosition = false;
    liveSources[liveSourceCount++] = s;
    return true;
}

void audio_source_awake(AudioSource *s)
{
    if (s->enabled && s->playOnAwake) audio_source_play(s);
}

// Unity 5.4 does not play a disabled source. The Player's strain sound is
// disabled in the scene and nothing enables it, so it never plays
// (INVENTORY.md section 2.5). Measured on the 2016 WebGL build: each Space
// press that starts a pull logs "Can not play a disabled audio source",
// and no buffer of player_strain starts.
void audio_source_play(AudioSource *s)
{
    if (!s->enabled || !audio_source_load(s)) return;
    if (s->loop) {
        StopMusicStream(s->music);
        PlayMusicStream(s->music);
    } else {
        PlaySound(s->sound);
    }
    audio_source_update(s);
}

void audio_source_stop(const AudioSource *s)
{
    if (!s->loaded) return;
    if (s->loop) StopMusicStream(s->music);
    else StopSound(s->sound);
}

bool audio_source_is_playing(const AudioSource *s)
{
    if (!s->loaded) return false;
    return s->loop ? IsMusicStreamPlaying(s->music) : IsSoundPlaying(s->sound);
}

// Measured on the 2016 WebGL build: the footsteps and a rock's drag sound
// start from the beginning each time the flag goes on, and stop when it
// goes off.
void audio_source_set_enabled(AudioSource *s, bool enabled)
{
    if (s->enabled == enabled) return;
    s->enabled = enabled;
    if (!enabled) audio_source_stop(s);
    else if (s->playOnAwake) audio_source_play(s);
}

void audio_source_update(AudioSource *s)
{
    if (!s->loaded) return;
    // Unity takes a source's velocity from how far it moved this frame.
    Vector3 v = {0};
    if (s->hasLastPosition && Time.deltaTime > 0) {
        v = (Vector3){(s->position.x - s->lastPosition.x) / Time.deltaTime,
                      (s->position.y - s->lastPosition.y) / Time.deltaTime,
                      (s->position.z - s->lastPosition.z) / Time.deltaTime};
    }
    s->lastPosition = s->position;
    s->hasLastPosition = true;
    AudioMix m = audio_mix(s, listenerPosition, listenerTurned, listenerVelocity, v);
    if (s->loop) {
        SetMusicVolume(s->music, m.volume);
        SetMusicPan(s->music, m.pan);
        SetMusicPitch(s->music, m.pitch);
        UpdateMusicStream(s->music);
    } else {
        SetSoundVolume(s->sound, m.volume);
        SetSoundPan(s->sound, m.pan);
        SetSoundPitch(s->sound, m.pitch);
    }
}

static void audio_sources_update(void)
{
    for (int i = 0; i < liveSourceCount; i++) audio_source_update(liveSources[i]);
}

static void audio_source_unload(AudioSource *s)
{
    audio_source_stop(s);
    if (s->loop) UnloadMusicStream(s->music);
    else UnloadSoundAlias(s->sound);
    s->loaded = false;
}

void audio_source_destroy(AudioSource *s)
{
    for (int i = 0; i < liveSourceCount; i++) {
        if (liveSources[i] != s) continue;
        audio_source_unload(s);
        liveSources[i] = liveSources[--liveSourceCount];
        return;
    }
}

// A scene change destroys the scene's sources, which cuts their sound.
static void audio_scene_unload(void)
{
    for (int i = 0; i < liveSourceCount; i++) audio_source_unload(liveSources[i]);
    liveSourceCount = 0;
    listenerPosition = listenerVelocity = (Vector3){0};
    listenerTurned = listenerPlaced = false;
}

void audio_listener_set_volume(float volume)
{
    listenerVolume = volume;
    if (IsAudioDeviceReady()) SetMasterVolume(volume * AUDIO_MASTER_VOLUME);
}

float audio_listener_volume(void)
{
    return listenerVolume;
}

void audio_listener_set(Vector3 position, bool turned)
{
    listenerVelocity = (Vector3){0};
    if (listenerPlaced && Time.deltaTime > 0) {
        listenerVelocity = (Vector3){(position.x - listenerPosition.x) / Time.deltaTime,
                                     (position.y - listenerPosition.y) / Time.deltaTime,
                                     (position.z - listenerPosition.z) / Time.deltaTime};
    }
    listenerPosition = position;
    listenerTurned = turned;
    listenerPlaced = true;
}

// --- Misc -------------------------------------------------------------------

void application_open_url(const char *url)
{
    OpenURL(url);
}
