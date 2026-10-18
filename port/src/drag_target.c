// SnapToTarget.cs, on the Game scene's RockDragTarget: the ring that marks
// the rock the player pulls, or would pull. Its shape tells a dash from a
// dot.
#include "game.h"

SnapToTarget dragTarget;

// original: // Use this for initialization
void drag_target_start(void)
{
    // The scene's instance, not the prefab: its alpha is 0.353, the
    // prefab's 0.627 (INVENTORY.md section 9, item 16).
    dragTarget = (SnapToTarget){
        .renderer = SPRITE_RENDERER(DRAG_TARGET_SCENE),
        .target = ROCK_NONE,
    };
}

// original: // Update is called once per frame
void drag_target_update(void)
{
    Vector3 pos = dragTarget.renderer.transform.position;
    const Rock *target = rock_get(dragTarget.target);
    if (target != NULL) {

        dragTarget.isDash = target->dark;


        Vector2 t = rock_position(target);
        pos.x = t.x;
        pos.y = t.y;
        dragTarget.renderer.transform.position = pos;

        dragTarget.renderer.enabled = true;

        dragTarget.renderer.sprite = dragTarget.isDash ? DRAG_TARGET_DASH_SPRITE : DRAG_TARGET_DOT_SPRITE;

    } else {

        dragTarget.renderer.enabled = false;
        // original: //enabled = false;
    }
}
