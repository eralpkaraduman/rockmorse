// The Game scene. No script sets an execution order, so Unity's order was
// undefined. The port fixes this one (plan section 6.3, item 10):
// - Start: rover, player, drag marker, camera, rock generator, rock
//   dragger, Mars (tiles and rocks), LevelLoader, RoverScreen, Earth (shows
//   the first tutorial). RoverScreen's Start reads Mars, which then still
//   has its scene values, as it would in any order in Unity.
// - FixedUpdate: TopDownControls, then the physics step and its handlers.
// - Update: RoverScreen, Mars, Earth, LevelLoader, RockDragger, each Rock,
//   SnapToTarget, CamerFocusSwitcher, then the Animator and the shatter
//   effects, which Unity runs after the scripts. RoverScreen comes before
//   Mars, so the sol counter shows this frame's life support (plan section
//   6.3, item 10). Last, the listener and the player's sounds move to the
//   player, for the audio update at the end of the frame.
// - LateUpdate: CutsSceneScaleFixer. Then deferred destroys.
#include "game.h"
#include "scenes.h"

static AudioSource menu4;
static SpriteList sprites;

void sprite_list_add(SpriteList *list, SpriteRenderer *r)
{
    if (list->count < GAME_MAX_SPRITES) list->items[list->count++] = r;
}

// Every trigger and collision event, once for each owner.
static void on_physics(PhysicsEvent e, const Collider *self, const Collider *other)
{
    switch ((Owner)self->kind) {
    case OWNER_PLAYER: // RockDragger is the Player's only handler
        if (e == TRIGGER_ENTER || e == TRIGGER_EXIT) rock_dragger_on_event(e, other);
        break;
    case OWNER_ROCK:
        rock_on_event(&rocks[self->index], e, other);
        break;
    case OWNER_ROVER_BODY:
    case OWNER_ROVER_INSIDE:
        rover_on_event((Owner)self->kind, e, other);
        break;
    case OWNER_ROCKET_COVER:
    case OWNER_ROCKET_INSIDE:
        rocket_on_event((Owner)self->kind, e, other);
        break;
    case OWNER_PLACEHOLDER:
        rock_placeholder_on_event(&rockPlaceholders[self->index], e, other);
        break;
    case OWNER_ROVER_ROOF:
    case OWNER_ROVER_REPELLER:
    case OWNER_ROCKET_BODY:
    case OWNER_ROCKET_REPELLER:
        break; // no script
    }
}

static void start(void)
{
    physics_shutdown();
    physics_init(on_physics);
    memset(rocks, 0, sizeof rocks);
    rock_placeholders_reset();
    shatters_reset();
    rockGenerator = (RockGenerator){.numRocks = ROCK_GENERATOR_NUM_ROCKS};
    rocket_reset();

    menu4 = AUDIO_SOURCE(GAME_START_SOUND);
    audio_source_awake(&menu4);

    rover_start();
    player_start();
    drag_target_start();
    camera_focus_start();
    rock_generator_start();
    rock_dragger_start();
    mars_start();
    level_loader_start();
    rover_screen_start();
    earth_start();
}

static void fixed_update(void)
{
    player_fixed_update();
}

static void physics(void)
{
    physics_step();
    player_sync();
    rocks_sync();
}

static void update(void)
{
    rover_screen_update();
    mars_update();
    earth_update();
    level_loader_update();
    rock_dragger_update();
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].alive) rock_update(&rocks[i]);
    }
    drag_target_update();
    camera_focus_update();

    animator_update(&player.animator, Time.deltaTime);
    player.renderer.sprite = animator_sprite(&player.animator);
    shatters_update();
    player_audio_update();
}

static void late_update(void)
{
    camera_focus_late_update();
}

static void end_of_frame(void)
{
    rock_placeholders_end_of_frame();
    rocks_end_of_frame();
}

static void draw(void)
{
    camera_clear(&focus.camera);
    sprites.count = 0;
    mars_sprites(&sprites);
    rover_sprites(&sprites);
    rocket_sprites(&sprites);
    rock_placeholders_sprites(&sprites);
    sprite_list_add(&sprites, &player.renderer);
    rocks_sprites(&sprites);
    // After the rocks: at the same z the particles draw over them, as
    // measured on the 2016 WebGL build.
    shatters_sprites(&sprites);
    sprite_list_add(&sprites, &dragTarget.renderer);
    camera_focus_sprites(&sprites);
    draw_sprites(&focus.camera, sprites.items, sprites.count);
    // The decoded letters and the cutscene images all have z -29, nearer
    // than every sprite above.
    level_loader_draw(&focus.camera);
    sprites.count = 0;
    camera_focus_cutscene_sprites(&sprites);
    draw_sprites(&focus.camera, sprites.items, sprites.count);

    // GUI elements in front of the scene, the higher z in front: the rover
    // screen's background has z -30, the visibility indicator -25, the
    // texts 0.
    gui_texture_draw(&roverScreen.background);
    gui_text_draw(&roverScreen.visibilityIndicator);
    gui_text_draw(&mars.solCounterText);
    gui_text_draw(&roverScreen.screenGUIText);
}

const Scene GAME_SCENE = {
    .name = "Game",
    .start = start,
    .fixed_update = fixed_update,
    .physics = physics,
    .update = update,
    .late_update = late_update,
    .end_of_frame = end_of_frame,
    .draw = draw,
};
