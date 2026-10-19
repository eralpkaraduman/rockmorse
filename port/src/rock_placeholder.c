// RockPlaceholder.cs, on the RockPlaceholderPrefab: one dot or dash slot of
// the answer. A rock of the right colour that rolls in turns it green, a
// wrong one red. In its first 0.6 s it pushes away any rock it touches, so
// a new answer starts clear.
#include "game.h"

RockPlaceholder rockPlaceholders[MAX_PLACEHOLDERS];

// original: //public LevelLoader levelLoader;
// (rockDragger and rocks are set or left null and never read.)

// original: // Use this for initialization
static void Start(RockPlaceholder *p)
{
    p->markTimeCreated = Time.time;

    // correctAudioSource = GetComponent<AudioSource>(): its own.
    p->correctAudioSource = AUDIO_SOURCE(PLACEHOLDER_CORRECT);
    audio_source_awake(&p->correctAudioSource);
}

// original: // Update is called once per frame
// (RockPlaceholder.Update is empty.)

// Unity runs Start before the next Update. The port runs it right away:
// LevelLoader makes placeholders from a trigger handler, where Time.time is
// the fixed time, at most one fixed step (0.02 s) before the next Update.
RockPlaceholder *rock_placeholder_instantiate(Vector3 position)
{
    RockPlaceholder *p = NULL;
    for (int i = 0; i < MAX_PLACEHOLDERS && !p; i++) {
        if (!rockPlaceholders[i].alive && !rockPlaceholders[i].destroyQueued) p = &rockPlaceholders[i];
    }
    if (!p) {
        TraceLog(LOG_WARNING, "more than %d placeholders", MAX_PLACEHOLDERS);
        return NULL;
    }
    *p = (RockPlaceholder){.alive = true, .snappedRock = ROCK_NONE, .clearTime = PLACEHOLDER_CLEAR_TIME};
    p->spriteRenderer = SPRITE_RENDERER(PLACEHOLDER);
    p->spriteRenderer.transform.position = position;
    // The CircleCollider2D trigger. The prefab has no Rigidbody2D, so the
    // collider is static.
    p->body = physics_body(b2_staticBody, (b2Vec2){position.x, position.y}, 0, 0, 0, false);
    physics_circle(p->body, (Collider){OWNER_PLACEHOLDER, (int)(p - rockPlaceholders), TAG_UNTAGGED, PLACEHOLDER_IS_TRIGGER},
                   (b2Vec2){PLACEHOLDER_OFFSET_WORLD_X, PLACEHOLDER_OFFSET_WORLD_Y}, PLACEHOLDER_RADIUS_WORLD);
    Start(p);
    return p;
}

static void setSprite(RockPlaceholder *p, int sprite)
{
    p->spriteRenderer.sprite = sprite;
}

void rock_placeholder_set_dash(RockPlaceholder *p, bool dash)
{
    p->_isDash = dash;
    setSprite(p, p->_isDash ? PLACEHOLDER_DASH_SPRITE : PLACEHOLDER_DOT_SPRITE);
}

bool rock_placeholder_is_dash(const RockPlaceholder *p)
{
    return p->_isDash;
}

static void snap(RockPlaceholder *p, const Rock *r)
{

    p->snappedRock = rock_ref(r);

    // original: //rockDragger.releaseIfDragging (r.transform);

    // original: /*
    // original: 		Vector3 pos = r.transform.position;
    // original: 		pos.x = transform.position.x;
    // original: 		pos.y = transform.position.y;
    // original: 		snappedRock.transform.position = pos;
    // original: 		snappedRock.transform.rotation = Quaternion.identity;
    // original: 		//snappedRock.setFixed(true);
    // original: 		*/
}

static void OnTriggerEnter2D(RockPlaceholder *p, const Collider *o)
{
    // o.GetComponent<Rock>(): only a rock's collider has one.
    Rock *r = o->kind == OWNER_ROCK ? &rocks[o->index] : NULL;
    if (r != NULL) {

        if (Time.time < (p->markTimeCreated + p->clearTime)) { // original: // clear it

            rock_relocate(r);
            return;
        }

        if (r->dark == p->_isDash) {
            snap(p, r);
            audio_source_play(&p->correctAudioSource);
            setSprite(p, PLACEHOLDER_CORRECT_SPRITE);
        } else if (rock_get(p->snappedRock) == NULL) {
            setSprite(p, PLACEHOLDER_INCORRECT_SPRITE);
        }


    }

}


bool rock_placeholder_is_correct(const RockPlaceholder *p)
{
    // original: //SpriteRenderer spriteRenderer = transform.GetComponent ("SpriteRenderer") as SpriteRenderer;
    return p->spriteRenderer.sprite == PLACEHOLDER_CORRECT_SPRITE;
}

static void OnTriggerExit2D(RockPlaceholder *p, const Collider *o)
{

    const Rock *r = o->kind == OWNER_ROCK ? &rocks[o->index] : NULL;
    if (r != NULL) {
        // A snapped rock that was destroyed reads as null, so it can
        // no longer equal r.
        if (rock_get(p->snappedRock) == r) {
            // original: //snappedRock.setFixed(false);
            p->snappedRock = ROCK_NONE;
            rock_placeholder_set_dash(p, p->_isDash);
        } else if (rock_get(p->snappedRock) == NULL) {
            rock_placeholder_set_dash(p, p->_isDash);
        }
    }

}

void rock_placeholder_on_event(RockPlaceholder *p, PhysicsEvent e, const Collider *other)
{
    if (e == TRIGGER_ENTER) OnTriggerEnter2D(p, other);
    else if (e == TRIGGER_EXIT) OnTriggerExit2D(p, other);
}

// Destroy(gameObject): the placeholder goes at the end of the frame.
void rock_placeholder_destroy(RockPlaceholder *p)
{
    p->destroyQueued = true;
}

void rock_placeholders_reset(void)
{
    memset(rockPlaceholders, 0, sizeof rockPlaceholders);
}

void rock_placeholders_end_of_frame(void)
{
    for (int i = 0; i < MAX_PLACEHOLDERS; i++) {
        RockPlaceholder *p = &rockPlaceholders[i];
        if (!p->destroyQueued) continue;
        physics_destroy_body(p->body);
        audio_source_destroy(&p->correctAudioSource);
        p->alive = false;
        p->destroyQueued = false;
    }
}

void rock_placeholders_sprites(SpriteList *list)
{
    for (int i = 0; i < MAX_PLACEHOLDERS; i++) {
        if (rockPlaceholders[i].alive) sprite_list_add(list, &rockPlaceholders[i].spriteRenderer);
    }
}
