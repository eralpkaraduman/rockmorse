#include "scenes.h"
#include "rlgl.h"

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

// Game scene placeholder until step 3 of the plan: the camera background
// colour, nothing else.
static UnityCamera gameCamera;

static void game_start(void)
{
    gameCamera = CAMERA(GAME_CAMERA);
}

static void game_draw(void)
{
    camera_clear(&gameCamera);
}

static const Scene GAME_SCENE = {.name = "Game", .start = game_start, .draw = game_draw};

// Build order, settings/EditorBuildSettings.json.
static const Scene *const SCENES[SCENE_COUNT] = {
    [SCENE_INTRO] = &INTRO_SCENE,
    [SCENE_TITLE] = &TITLE_SCENE,
    [SCENE_STORY1] = &STORY1_SCENE,
    [SCENE_STORY2] = &STORY2_SCENE,
    [SCENE_GAME] = &GAME_SCENE,
    [SCENE_DEATH] = &DEATH_SCENE,
    [SCENE_WIN] = &WIN_SCENE,
};

static RenderTexture2D screen;
static double accumulator;

// Picks where to draw the 660x440 game image in the window.
// Each game pixel covers a whole number of physical screen pixels, so pixel
// art stays sharp on any display density. Leftover space becomes black bars.
static Rectangle pixel_perfect_rect(void)
{
#if defined(PLATFORM_WEB)
    Vector2 dpi = {1, 1}; // the browser handles density, see shell.html
#else
    Vector2 dpi = GetWindowScaleDPI(); // 2 on a Retina display, 1 on most others
#endif
    int physicalW = GetRenderWidth();
    int physicalH = GetRenderHeight();
    int scale = physicalW / WINDOW_WIDTH < physicalH / WINDOW_HEIGHT ? physicalW / WINDOW_WIDTH : physicalH / WINDOW_HEIGHT;
    if (scale < 1) scale = 1;

    // raylib takes drawing coordinates in logical points, not physical pixels.
    float w = (float)(WINDOW_WIDTH * scale) / dpi.x;
    float h = (float)(WINDOW_HEIGHT * scale) / dpi.y;
    return (Rectangle){(GetScreenWidth() - w) / 2, (GetScreenHeight() - h) / 2, w, h};
}

// Runs Unity frames at a fixed 60 per second, whatever the display rate.
static void frame(void)
{
    Rectangle dest = pixel_perfect_rect();
    input_set_mouse_mapping((Vector2){dest.x, dest.y}, WINDOW_WIDTH / dest.width);
    input_poll();

    accumulator += GetFrameTime();
    if (accumulator > 0.25) accumulator = 0.25; // avoid a spiral after a stall
    while (accumulator >= UNITY_FRAME_TIME) {
        unity_frame();
        input_clear();
        accumulator -= UNITY_FRAME_TIME;
    }

    BeginTextureMode(screen);
    unity_draw();
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    // Render textures are stored upside down, so flip the source height.
    Rectangle source = {0, 0, WINDOW_WIDTH, -WINDOW_HEIGHT};
    // Copy the game image without blending. Its alpha is below 1 where
    // antialiased text was blended in, and blending again would darken
    // those edges (measured: 84 instead of the reference's 112).
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    DrawTexturePro(screen.texture, source, dest, (Vector2){0, 0}, 0, WHITE);
    EndBlendMode();
    EndDrawing();
}

int main(void)
{
#if defined(PLATFORM_WEB)
    // On the web the canvas is exactly the game's size, one canvas pixel per
    // game pixel. The page scales it up, see shell.html.
    SetConfigFlags(FLAG_VSYNC_HINT);
#else
    // The original window was 660x440 and fixed. The port's window can grow,
    // and the game scales by whole numbers inside it.
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#endif
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, PRODUCT_NAME);
#if !defined(PLATFORM_WEB)
    ChangeDirectory(GetApplicationDirectory()); // assets sit next to the binary
    SetTargetFPS(UNITY_FRAME_RATE);
#endif
    InitAudioDevice();
    screen = LoadRenderTexture(WINDOW_WIDTH, WINDOW_HEIGHT);

    unity_init(SCENES, SCENE_INTRO);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
#endif

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
