// The Game scene. No script sets an execution order, so Unity's order was
// undefined. The port fixes this one (plan section 6.3, item 10):
// - Start: rover, player, drag marker, camera, rover screen, rock generator,
//   rock dragger, Mars (tiles and rocks), Earth (shows the first tutorial).
// - FixedUpdate: TopDownControls, then the physics step and its handlers.
// - Update: Mars, Earth, RockDragger, each Rock, SnapToTarget,
//   CamerFocusSwitcher, then the Animator.
// - LateUpdate: CutsSceneScaleFixer. Then deferred destroys.
#include "game.h"
#include "scenes.h"

LevelLoader levelLoader;

static AudioSource menu4;
static SpriteList sprites;

void sprite_list_add(SpriteList *list, SpriteRenderer *r)
{
    if (list->count < GAME_MAX_SPRITES) list->items[list->count++] = r;
}

// LevelLoader.setDecodedLettersHidden: the letters come in step 5.
void level_loader_set_decoded_letters_hidden(bool hidden)
{
    (void)hidden;
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
    rockGenerator = (RockGenerator){.numRocks = ROCK_GENERATOR_NUM_ROCKS};
    levelLoader = (LevelLoader){
        .position = {LEVEL_POS_X, LEVEL_POS_Y, LEVEL_POS_Z},
        .sentenceSize = {LEVEL_LOADER_SENTENCE_SIZE_X, LEVEL_LOADER_SENTENCE_SIZE_Y},
    };
    rocket_reset();

    menu4 = AUDIO_SOURCE(GAME_START_SOUND);
    audio_source_awake(&menu4);

    rover_start();
    player_start();
    drag_target_start();
    camera_focus_start();
    rover_screen_start();
    rock_generator_start();
    rock_dragger_start();
    mars_start();
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
    mars_update();
    earth_update();
    rock_dragger_update();
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].alive) rock_update(&rocks[i]);
    }
    drag_target_update();
    camera_focus_update();

    animator_update(&player.animator, Time.deltaTime);
    player.renderer.sprite = animator_sprite(&player.animator);
}

static void late_update(void)
{
    camera_focus_late_update();
}

static void end_of_frame(void)
{
    rocks_end_of_frame();
}

static void draw(void)
{
    camera_clear(&focus.camera);
    sprites.count = 0;
    mars_sprites(&sprites);
    rover_sprites(&sprites);
    rocket_sprites(&sprites);
    sprite_list_add(&sprites, &player.renderer);
    rocks_sprites(&sprites);
    sprite_list_add(&sprites, &dragTarget.renderer);
    camera_focus_sprites(&sprites);
    draw_sprites(&focus.camera, sprites.items, sprites.count);

    // GUI elements in front of the scene, the higher z in front: the rover
    // screen's background has z -30, the texts 0.
    gui_texture_draw(&roverScreen.background);
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
