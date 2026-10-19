# Unused files

Super Damage made these files in 2014 for Rockmorse, but the game never used them. No scene, prefab or script references them, and the 2016 builds do not contain them. They are kept here for history. The port does not read them.

The paths are the ones under `Assets/` in the Unity project (`Unity/LD48-Unity` in the superdamage/LD30 repo).

- `Levels/levelDesignDraft.txt`: the first draft of the story, with 8 messages and their answers. The game's final text is in `Levels/levels.json`, which the port reads (`port/src/tables.h`).
- `Levels/level1.json`: a single test level with the answer "HELLO".
- `Scripts/CameraFollowBounds.cs`: a camera that follows the player inside the map's edges. It is attached to nothing (`INVENTORY.md` section 2.19).
- `Sprites/bis_dude_v1.png`: an early player sprite.
- `Sprites/bis_rock_light_v3 backup.png`: a backup of the light rock sprite.
- `Sprites/ground/bis_ground_tile_v2_1.png` to `_3.png`: three ground tiles. The game uses the `marsTiles.png` sheet instead.
