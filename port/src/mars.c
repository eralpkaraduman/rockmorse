// Mars.cs, on the Game scene's Mars object. It lays out the 66 x 44 ground
// tiles, asks RockGenerator for the rocks, and runs the sol clock that sets
// the darkness.
#include "game.h"

Mars mars;

#define TILE_COUNT (MARS_NUM_TILES_X * MARS_NUM_TILES_Y)
static SpriteRenderer tiles[TILE_COUNT];
static int tileCount;

// Mars.cs private fields.
static int U; // original: //X
static int V; // original: //Y
static float spriteSize;
static float x;
static float y;
static float genTotal;
static float gen;
static Vector2 topLeft;
static float SOL_IN_HOURS;
static float SOL_IN_SECONDS;
static float timePastWhenFrozen;
static float timeOffset;
static float timeSceneEntered;
static float lastTimeSolCounterDisplayed;
static float solCounterDuration;

static void newSol(void);

static void GenerateJob(void)
{
    while (V > 0) {

        while (U > 0) {

            Vector3 pos = {x, y, 0};
            // Instantiate(tileTemplate, pos, Quaternion.identity): the
            // MarsTile prefab.
            SpriteRenderer *tile = &tiles[tileCount++];
            *tile = (SpriteRenderer){
                .transform = {.position = pos,
                              .scale = {MARS_TILE_SCALE_X, MARS_TILE_SCALE_Y, MARS_TILE_SCALE_Z}},
                .sprite = -1,
                .color = {MARS_TILE_COLOR_R, MARS_TILE_COLOR_G, MARS_TILE_COLOR_B, MARS_TILE_COLOR_A},
                .sortingOrder = MARS_TILE_SORTING_ORDER,
                .enabled = MARS_TILE_ENABLED,
            };
            // original: //Debug.Log(100-(gen/genTotal*100));
            gen--;
            (void)gen; // counted down, never read, as in the source


            int randomIndex = (int)mathf_round((float)random_range_int(0, MARS_TILE_SPRITES_COUNT));
            tile->sprite = MARS_TILE_SPRITES[randomIndex];

            tile->transform.scale = (Vector3){MARS_TILE_SIZE, MARS_TILE_SIZE, MARS_TILE_CODE_SCALE_Z};

            x += spriteSize;

            U--;

        }

        U = MARS_NUM_TILES_X;
        x = topLeft.x;
        y += spriteSize;
        V--;

    }

}

void mars_start(void)
{
    mars = (Mars){
        .secondsSpent = MARS_SECONDS_SPENT,
        .solCounterText = GUI_TEXT(SOL_COUNTER),
    };
    tileCount = 0;
    SOL_IN_HOURS = (MARS_SOL_HOURS_BASE * MARS_SOL_HOURS_FACTOR);
    timePastWhenFrozen = 0.0f;
    lastTimeSolCounterDisplayed = 0;
    solCounterDuration = MARS_SOL_COUNTER_DURATION;

    timeSceneEntered = Time.time;

    SOL_IN_SECONDS = SOL_IN_HOURS * MARS_MINUTES_PER_HOUR * MARS_SECONDS_PER_MINUTE;

    // original: // time offset 
    // original: //timeOffset = SOL_IN_SECONDS/4; // daybreak
    timeOffset = MARS_TIME_OFFSET;

    mars.worldBunds = (Rect){0};

    U = MARS_NUM_TILES_X;
    V = MARS_NUM_TILES_Y;

    genTotal = (float)(U * V);
    gen = genTotal;

    // sprites[0].bounds.size.x * tileSize
    spriteSize = SPRITES[MARS_TILE_SPRITES[0]].size_world_x * MARS_TILE_SIZE;

    Vector2 finalSize = {0};
    finalSize.x = spriteSize * (MARS_NUM_TILES_X - 1);
    finalSize.y = spriteSize * (MARS_NUM_TILES_Y - 1);

    topLeft.x = -(finalSize.x / 2);
    topLeft.y = -(finalSize.y / 2);

    x = topLeft.x;
    y = topLeft.y;

    mars.worldBunds.x = x;
    mars.worldBunds.y = y;
    mars.worldBunds.width = finalSize.x + spriteSize;
    mars.worldBunds.height = finalSize.y + spriteSize;

    // original: //ThreadStart job = new ThreadStart (GenerateJob);
    // original: //Thread thread = new Thread (job);
    // original: //thread.Start ();

    GenerateJob();

    rock_generator_generate(mars.worldBunds);
}

void mars_update(void)
{

    // original: // when game resets, time adds up so i had to subtract scene starting time
    float relativeTime = Time.time - timeSceneEntered - timePastWhenFrozen;

    int oldSol = mars.currentSol;

    if (mars.timeFrozen == false) {
        mars.secondsSpent = timeOffset + relativeTime * MARS_TIME_SCALE;
    } else {
        timePastWhenFrozen += Time.deltaTime;
    }

    mars.percentTimeLeftForNextSol = 1 - fmodf(mars.secondsSpent, SOL_IN_SECONDS) / SOL_IN_SECONDS;
    mars.secondsUntilNextSol = SOL_IN_SECONDS * mars.percentTimeLeftForNextSol;
    mars.hoursUntilNextSol = SOL_IN_HOURS * mars.percentTimeLeftForNextSol;
    mars.currentSol = (int)ceilf(mars.secondsSpent / SOL_IN_SECONDS);

    gui_text_set(&mars.solCounterText, TextFormat("%s%d%s%d%s", MARS_SOL_COUNTER_PREFIX, mars.currentSol,
                                                  MARS_SOL_COUNTER_NEWLINE, roverScreen.lifeSupport,
                                                  MARS_SOL_COUNTER_SUFFIX));


    mars.darkness = fabsf(((1 - mars.percentTimeLeftForNextSol) - MARS_DARKNESS_OFFSET) / MARS_DARKNESS_DIVISOR);
    mars.isEarthVisible = mars.darkness <= MARS_EARTH_VISIBLE_MAX_DARKNESS_DOUBLE;

    if (oldSol != mars.currentSol) {
        newSol();
    }

    if (Time.time > lastTimeSolCounterDisplayed + solCounterDuration) {
        mars.solCounterText.enabled = false;
    }
}

static void newSol(void)
{

    // original: //Color c = solCounterText.color;
    // original: //c.a = 1;
    mars.solCounterText.enabled = true;

    lastTimeSolCounterDisplayed = Time.time;

    earth_new_sol();
}

void mars_sprites(SpriteList *list)
{
    for (int i = 0; i < tileCount; i++) sprite_list_add(list, &tiles[i]);
}
