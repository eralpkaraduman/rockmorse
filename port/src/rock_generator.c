// RockGenerator.cs, on the Game scene's Rocks object. It scatters 100 rocks
// (the scene's numRocks; the script default is 40) and puts a new one
// down for every rock that goes.
#include "game.h"

RockGenerator rockGenerator;

static Vector3 randomPositon(void);

// original: // Use this for initialization
void rock_generator_start(void)
{
    // rocks = new List<Rock>(). Nothing reads that list, so the port keeps
    // none (plan section 6.3, item 10).
}

Rock *rock_generator_add_rock(bool dark) // addRock
{

    Vector3 pos = randomPositon();
    Rock *rockGO = rock_instantiate(pos);
    if (!rockGO) return NULL;
    rock_set_dark(rockGO, dark);
    // rock.parent = this.gameObject.transform, rockGO.generator = this:
    // the port has one generator.

    return rockGO;

}

void rock_generator_generate(Rect _worldBounds)
{
    rockGenerator.worldBounds = _worldBounds;

    while (rockGenerator.numRocks > 0) {

        bool dark = random_range_int(ROCK_GENERATOR_COLOUR_RANGE_MIN, ROCK_GENERATOR_COLOUR_RANGE_MAX) ==
                    ROCK_GENERATOR_DARK_WHEN;
        rock_generator_add_rock(dark);

        rockGenerator.numRocks--;

    }
}


static Vector3 randomPositon(void)
{
    float halfW = rockGenerator.worldBounds.width / 2 * ROCK_GENERATOR_SPAWN_FRACTION;
    float halfH = rockGenerator.worldBounds.height / 2 * ROCK_GENERATOR_SPAWN_FRACTION;
    float rX = random_range_float(-halfW, halfW);
    float rY = random_range_float(-halfH, halfH);

    Vector3 pos = {rX, rY, ROCKS_POS_Z};
    return pos;
}

void rock_generator_replace(Rock *rock)
{

    // Instantiate(rockShatterAnimationPrefab, rock.transform.position,
    // identity). This runs in a physics handler, where Unity's transform
    // already has the body's new position.
    Vector2 at = rock_position(rock);
    shatter_instantiate((Vector3){at.x, at.y, rock->renderer.transform.position.z});

    rock_destroy(rock);
    rockGenerator.numRocks++;
    rock_generator_generate(rockGenerator.worldBounds);

}

// original: // Update is called once per frame
// RockGenerator.Update is empty.
