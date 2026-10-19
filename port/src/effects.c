// RockShatterPrefab, the puff a rock leaves when it relocates. Its child
// "large flames" is a legacy particle system: an EllipsoidParticleEmitter, a
// ParticleAnimator and a ParticleRenderer, with a legacy Animation clip
// ("Stop Emitting ,5 sec") that turns the emitter off. The root plays
// Explosion1 and carries TimedObjectDestructor.js, which destroys it after
// timeOut seconds.
//
// The rules below were measured on the 2016 WebGL build, with rocks relocated
// one at a time through Unity's SendMessage and every frame captured:
// - The cloud covers about half the pixels that 100 particles a second
//   would give, and 1.2 times what one particle per frame gives: each frame
//   emits the whole part of 100 * deltaTime, 1, and drops the rest.
// - New particles show from the first frame, and the last one goes 0.80 to
//   0.83 s after the shatter. So the emitter runs until the curve reaches
//   0 at 0.5 s: m_Emit is on while the curve is above 0. A cut at 0.5
//   would end every effect by 0.65 s.
// - The cloud grows 4 px a frame at first and slows to about 3 by 0.3 s.
//   That fits rndVelocity as up to 5 units/s each way and damping applied
//   per second, velocity *= damping ^ deltaTime. Damping per frame would
//   stop every particle within 9 px of the rock.
// - A particle is the dark rock texture times the scene's ambient light,
//   at alpha 0.42: the Alpha-Diffuse shader ignores the colour keys
//   (INVENTORY.md 7.5). A lone particle over the ground solved to
//   (51, 26, 17) out of 255 before the night overlay; the texture times the
//   ambient light is (57, 29, 18).
// - Particles draw over rocks, which have the same z.
// - At timeScale 0 the effect waits: shatters from the first physics step
//   under the tutorial play out after Space.
#include "game.h"

Shatter shatters[MAX_SHATTERS];

// The Game scene's ambient light is not in values.h yet. These are the
// dump's values, scenes/Game.json#scene_settings/RenderSettings:m_AmbientLight,
// used until extract.py adds the row (step 10).
#ifndef GAME_AMBIENT_LIGHT_R
#define GAME_AMBIENT_LIGHT_R 1.0f
#define GAME_AMBIENT_LIGHT_G 0.6710954f
#define GAME_AMBIENT_LIGHT_B 0.49264705f
#endif

// The sprite that shows the material's whole texture.
static int texture_sprite(void)
{
    for (int i = 0; i < SPR_COUNT; i++) {
        if (SPRITES[i].image == SHATTER_MATERIAL_MAIN_TEX) return i;
    }
    return -1;
}

void shatters_reset(void)
{
    memset(shatters, 0, sizeof shatters);
}

// Instantiate(rockShatterAnimationPrefab, position, identity). Awake of
// TimedObjectDestructor calls Invoke("DestroyNow", timeOut), the
// AudioSource plays on awake and the Animation plays automatically.
Shatter *shatter_instantiate(Vector3 position)
{
    Shatter *s = NULL;
    for (int i = 0; i < MAX_SHATTERS && !s; i++) {
        if (!shatters[i].alive) s = &shatters[i];
    }
    if (!s) {
        // ponytail: a fixed pool. The rocket landing on the rocks of
        // earlier answers shatters 30 or more in one step; raise
        // MAX_SHATTERS if the warning shows.
        TraceLog(LOG_WARNING, "more than %d shatter effects", MAX_SHATTERS);
        return NULL;
    }
    *s = (Shatter){.alive = true, .position = position, .emit = SHATTER_EMIT};
    s->sound = AUDIO_SOURCE(SHATTER_SOUND);
    s->sound.rolloffCurve = SHATTER_SOUND_ROLLOFF_CURVE;
    s->sound.position = position;
    audio_source_awake(&s->sound);
    return s;
}

// One frame of the effect, in game time.
void shatter_update(Shatter *s, float deltaTime)
{
    if (!s->alive || deltaTime <= 0) return;
    s->time += deltaTime;

    // The Animation clip sets m_Emit. It plays once, so after 0.5 s it
    // holds the curve's last value, 0.
    s->emit = curve_evaluate(SHATTER_STOP_EMIT_CURVE, SHATTER_STOP_EMIT_CURVE_COUNT, s->time) > 0;

    // EllipsoidParticleEmitter. The ellipsoid has size 0, so particles start
    // at the emitter, and its world, local and tangent velocities are 0. A
    // frame emits the whole part of rate * deltaTime and drops the rest:
    // one particle per 60 Hz frame, not 1.67 (see the top of the file).
    if (s->emit) {
        int count = (int)(random_range_float(SHATTER_MIN_EMISSION, SHATTER_MAX_EMISSION) * deltaTime);
        for (int i = 0; i < count && s->particleCount < SHATTER_MAX_PARTICLES; i++) {
            s->particles[s->particleCount++] = (Particle){
                .position = {s->position.x, s->position.y},
                .velocity = {random_range_float(-SHATTER_RND_VELOCITY_X, SHATTER_RND_VELOCITY_X),
                             random_range_float(-SHATTER_RND_VELOCITY_Y, SHATTER_RND_VELOCITY_Y)},
                .energy = random_range_float(SHATTER_MIN_ENERGY, SHATTER_MAX_ENERGY),
                .size = random_range_float(SHATTER_MIN_SIZE, SHATTER_MAX_SIZE),
                .rotation = SHATTER_RND_ROTATION ? random_range_float(0, 360) : 0,
            };
        }
    }

    // ParticleAnimator: force, then damping per second, then the move. Size
    // grow, random force and the rotation axes are 0. The z of a particle
    // only moves it along the view, and the renderer does not sort by it,
    // so the port keeps x and y.
    float damping = powf(SHATTER_ANIM_DAMPING, deltaTime);
    int kept = 0;
    for (int i = 0; i < s->particleCount; i++) {
        Particle p = s->particles[i];
        p.energy -= deltaTime;
        if (p.energy <= 0) continue;
        p.velocity.x = (p.velocity.x + SHATTER_ANIM_FORCE_X * deltaTime) * damping;
        p.velocity.y = (p.velocity.y + SHATTER_ANIM_FORCE_Y * deltaTime) * damping;
        p.position.x += p.velocity.x * deltaTime;
        p.position.y += p.velocity.y * deltaTime;
        s->particles[kept++] = p;
    }
    s->particleCount = kept;
    // ParticleAnimator.autodestruct removes "large flames" once it is
    // empty and done emitting. Nothing of it shows after that, so the port
    // waits for the root's timer.

    // TimedObjectDestructor.DestroyNow. detachChildren is false.
    if (s->time >= SHATTER_TIME_OUT) {
        audio_source_destroy(&s->sound);
        s->alive = false;
    }
}

void shatters_update(void)
{
    for (int i = 0; i < MAX_SHATTERS; i++) shatter_update(&shatters[i], Time.deltaTime);
}

// ParticleRenderer: one camera-facing square per particle, size units wide,
// in the order they were emitted. All of them sort at the effect's z.
void shatters_sprites(SpriteList *list)
{
    int sprite = texture_sprite();
    float spriteSize = sprite >= 0 ? SPRITES[sprite].size_world_x : 1;
    for (int i = 0; i < MAX_SHATTERS; i++) {
        Shatter *s = &shatters[i];
        if (!s->alive) continue;
        for (int k = 0; k < s->particleCount; k++) {
            const Particle *p = &s->particles[k];
            float scale = p->size / spriteSize;
            s->sprites[k] = (SpriteRenderer){
                .transform = {.position = {p->position.x, p->position.y, s->position.z},
                              .scale = {scale, scale, 1}, .angle = p->rotation},
                .sprite = sprite,
                .color = {SHATTER_MATERIAL_COLOR_R * GAME_AMBIENT_LIGHT_R, SHATTER_MATERIAL_COLOR_G * GAME_AMBIENT_LIGHT_G,
                          SHATTER_MATERIAL_COLOR_B * GAME_AMBIENT_LIGHT_B, SHATTER_MATERIAL_COLOR_A},
                .enabled = true,
            };
            sprite_list_add(list, &s->sprites[k]);
        }
    }
}
