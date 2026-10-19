# Rockmorse inventory

This is the exact map of what the C port must implement. It was written from the Unity project, the 2016 Mac build and the 2016 WebGL build. Steps 2 to 8 of the port measured the engine behaviour the data cannot show. Section 11 has the results.

How to read the citations:

- `Earth.cs:73` is a line in `Assets/Scripts/` of the Unity project (`Unity/LD48-Unity` in the superdamage/LD30 repo).
- `scenes/Game.json#Player:Rigidbody2D:m_Mass` is a value in the dumps under `extract/`. The dump path is `<file>#<GameObject path>:<component>:<field>`.
- `VALUE_NAME` in capitals is a constant in `port/src/values.h`. Its comment names the same dump path. Step 10 deleted the rows that no C code reads. A name of that kind no longer exists in `values.h`, but its value is still in the dump.
- "The build" means the 2016 Mac and WebGL builds. "The source" means the Unity project.
- `extract/build/` holds facts that exist only in the 2016 Mac build: `audio.json`, `shaders.json`, `font.json`, `color_space.json`. `extract/source_comments.json` holds every comment of the game scripts, verbatim. `port/src/code_constants.h` holds the strings and numbers that live only in the C# code. `tools/build_extract.py` reads the build for `tools/extract.py`.

Facts that came from the dumps, not from reading code, are in tables. Facts I could not confirm are marked "unverified".

## 1. Source versus build

### What was compared

`tools/compare_build.py` loads the Mac player (`level0` to `level6`, `sharedassets*.assets`, `globalgamemanagers`, `Assembly-CSharp.dll`) and the WebGL data bundle (the gunzipped `Release/RockMorse.datagz`, a Unity 5.4.3f1 bundle with the same files). It compares them with the dumps of the source. Reports: `extract/build_comparison.json` (Mac) and `extract/build_comparison_webgl.json` (WebGL).

Which is newer: the build. The scene and prefab files in the source are still in the old Unity 4.x binary format (headers say 4.6.1f1 for scenes, 4.5.2f1 for prefabs and settings). Unity 5.4.3f1 imported them in December 2016 and made the build, but nobody re-saved the scenes. So the build holds the values after Unity's own upgrade step.

### Result

- All 7 scenes and 7 of the 8 prefabs are in the build with the same 71 GameObjects (58 in the scenes, 13 in the prefabs), the same component lists and the same values. `RockDragTarget.prefab` is not in the build, because no script instantiates it. Only its scene instance exists.
- Every MonoBehaviour field matches. I decoded them from the raw bytes with the field layout of the C# source. All 27 script objects of the build (28 in the source; the missing one is on `RockDragTarget.prefab`) decode to exactly their byte size, so the field layout in the build equals the source.
- Transforms, colliders, rigid bodies, the joint, cameras, GUIText, TextMesh, sprites, clips, sprite slices, pivots, pixel sizes, filter modes and all project settings match.
- The WebGL build matches the Mac build in every gameplay value. Only the audio compression differs (WebGL uses AAC, format 7).

### The values that differ

| What | Source | Build | values.h |
|---|---|---|---|
| AudioSource spatial blend (`panLevelCustomCurve`) | 1.0 on all 18 sources | 1.0 on the 8 sources whose clip has the importer 3D flag, 0.0 on the 10 others | follows the build, `*_SPATIAL_BLEND`. The source value stays as `*_PAN_LEVEL_IN_SOURCE_SCENE` |
| AudioSource reverb zone mix (new field in Unity 5) | none | 1.0 on the 8 spatial sources, 0.0 on the others | follows the build, `*_REVERB_ZONE_MIX` |
| Rigidbody2D | `m_FixedAngle` | `m_Constraints` 4 on the Player, 0 on all others. `m_UseAutoMass` false | same meaning, no change |
| Colliders | `m_Center`, `m_Poly` | `m_Offset` (all 0 for polygons), `m_Points`, `m_Density` 1, `m_UsedByEffector` false | same meaning |
| DistanceJoint2D | `m_CollideConnected` false | `m_EnableCollision` false, `m_AutoConfigureDistance` false, `m_AutoConfigureConnectedAnchor` false, `m_BreakForce` Infinity | same meaning. Distance stays 0.55 |
| `m_LightmapIndex` | 255 | 65535 | not used |
| Level children order | rockDestructionEffect, letters | letters, rockDestructionEffect | not used |
| InputManager | 15 axes | 18 axes (Submit twice and Cancel are extra) | Horizontal and Vertical axes are identical |
| AudioManager | has speed of sound 347 | field removed | kept from the source |
| Physics2D | `m_RaycastsHitTriggers` | renamed `m_QueriesHitTriggers`, both true. New `m_ChangeStopsCallbacks` | kept from the source |
| static.mp3 sample rate | file header 11025 | Unity stores 11000 | not used |

The spatial blend rule is the one real behaviour difference. In Unity 4 a clip played in 3D only if the clip import setting "3D" was on. Unity 5 moved that into the AudioSource. The upgrade copied the clip flag into the source. Table in section 6.2.

### Textures in the build

48 sprites are ARGB32 (format 5). 8 images are RGB24 (format 3): `superdamage`, the three cutscene images, `death`, `win`, `bis_screen_v2` and `marsTiles`. The 3 tutorial images are DXT1 (format 10). The port uses the PNG files, which have the same pixel sizes.

### Assembly-CSharp.dll against the C# source

The logic check is done and is in `extract/decompile_check.md`. `ilspycmd` decompiled the three game DLLs and `tools/decompile_check.py` compared the result with the source. All 24 scripts MATCH. So do `TimedObjectDestructor.js` and the iTween and JSON code the game calls. The decompiled code is in `extract/decompiled/`. This section keeps the earlier metadata check. `tools/compare_build.py` still runs it, and it reads the .NET metadata in pure Python.

What was checked, for all 24 scripts:

- Every class exists in the DLL. Every field name and every method name of the source exists in the DLL, and the DLL has no other ones. The compiler adds a default constructor and the backing field of one event. Those are ignored.
- Every string literal of the source is in the DLL string heap (206 strings in the DLL).
- Every number in the source appears as a constant in the method bodies, after walking the IL byte code. 15 scripts match exactly. The other 9 differ only in ways the compiler explains: an int literal that became a float constant (`/5` is 5.0, `* 13` is 13.0, `dismissDuration = 5` is 5.0), constants the compiler folded (`24 * 1.027f` is 24.648), a minus sign moved into the constant (`-0.6f`, `-1.394218f`), key codes (`KeyCode.C` 99, `LeftShift` 304, `Space` 32, `H` 104, `P` 112, `Escape` 27), `Mathf.Infinity`, and the array sizes of `string.Concat` calls (4 and 5, my reading).
- The 25th script, `TimedObjectDestructor.js`, is UnityScript and is not in `Assembly-CSharp.dll`. Its two fields decode correctly from the bytes (`timeOut` 2.0, `detachChildren` false).

The method logic is covered by the decompile check (`extract/decompile_check.md`). Nothing is left unchecked in the game scripts.

## 2. Scripts

24 scripts are in `Assets/Scripts` (2,197 lines). One more, `TimedObjectDestructor.js`, is attached to a prefab (section 9, item 1).

Scenes where each script sits (from the dumps):

- CamerFocusSwitcher: scenes/Game `Main Camera`
- CutsSceneScaleFixer: scenes/Game `Main Camera/cutscenes`
- Earth: scenes/Game `Earth`
- EndGame: scenes/Death `gfx`; scenes/Win `gfx`
- GUITextButton: scenes/Title `PLAY`; scenes/Title `SD`
- InsideRocketTrigger: prefabs/RocketPrefab `RocketPrefab/InsideTrigger`
- Intro: scenes/Intro `Main Camera`
- LevelLoader: scenes/Game `Level`
- Mars: scenes/Game `Mars`
- NextSceneOnSpace: scenes/Story1 `gfx`; scenes/Story2 `gfx`
- Rock: prefabs/RockPrefab `RockPrefab`
- RockDragger: scenes/Game `Player`
- RockGenerator: scenes/Game `Rocks`
- RockPlaceholder: prefabs/RockPlaceholderPrefab `RockPlaceholderPrefab`
- Rocket: prefabs/LetterPrefab `LetterPrefab`; prefabs/RocketPrefab `RocketPrefab`
- Rover: scenes/Game `Rover/RoverBody`
- RoverInsideTrigger: scenes/Game `Rover/playerInsideRoverTrigger`
- RoverScreen: scenes/Game `RoverScreen`
- SnapToTarget: scenes/Game `RockDragTarget`; prefabs/RockDragTarget `RockDragTarget`
- TimedObjectDestructor: prefabs/RockShatterPrefab `RockShatterPrefab`
- Title: scenes/Title `Main Camera`
- TopDownControls: scenes/Game `Player`
- VisibleOnTrigger: prefabs/RocketPrefab `RocketPrefab/RocketCover`

`CameraFollowBounds` is attached to nothing. `Rocket` is also attached to `LetterPrefab` by mistake. It does nothing there (`Rocket.cs:19` runs only when called).

Unity runs scripts in an undefined order here. No script sets an execution order (`.meta` files: `executionOrder: 0`). The port picks one order and writes it down.

### 2.1 Summary table

| Script | Lines | C file | Notes |
|---|---|---|---|
| LevelLoader | 399 | `level_loader.c` | |
| Earth | 222 | `earth.c` | |
| Mars | 185 | `mars.c` | |
| RockDragger | 161 | `rock_dragger.c` | |
| Rock | 148 | `rock.c` | |
| RoverScreen | 130 | `rover_screen.c` | |
| RockPlaceholder | 113 | `rock_placeholder.c` | |
| CamerFocusSwitcher | 110 | `camera_focus.c` | also holds iTween.MoveUpdate and CutsSceneScaleFixer |
| Morse | 95 | `morse.c` | |
| TopDownControls | 89 | `player.c` | |
| RockGenerator | 76 | `rock_generator.c` | |
| RoverInsideTrigger | 59 | `rover.c` | merged with Rover and VisibleOnTrigger |
| Rover, VisibleOnTrigger | 70 | `rover.c` | |
| SnapToTarget | 41 | `drag_target.c` | |
| InsideRocketTrigger, Rocket | 60 | `rocket.c` | |
| Title, GUITextButton | 66 | `title.c` | |
| Intro, NextSceneOnSpace, EndGame | 88 | `scenes.c` | |
| CutsSceneScaleFixer | 22 | `camera_focus.c` | |
| CameraFollowBounds | 63 | none | attached to nothing |
| TimedObjectDestructor.js | 14 | `effects.c` | the 25th script, on the shatter prefab |
| iTween (one call), JSONObject | | `camera_focus.c`, `tables.h` | third party |

### 2.2 LevelLoader (`LevelLoader.cs`, 399 lines)

Scene: `scenes/Game.json#Level:LevelLoader`.

Fields and scene values:

| Field | Scene value | Script default | Constant |
|---|---|---|---|
| `currentLevel` | -1 | -1 | `LEVEL_LOADER_CURRENT_LEVEL` |
| `gap` | (1, 3) | (3, 3) | `LEVEL_LOADER_GAP_X`, `_Y` |
| `currentQuestion` | "We cant hear you,are you ok? We will think of a solution, calm down and wait   for our response." | "" | `LEVEL_LOADER_INITIAL_QUESTION` |
| `sentenceSize` | (0, 0) | (0, 0) | |
| `levelsAsset` | `Levels/levels.json` | | converted to `LEVELS` in `tables.h` |
| `morseTextAsset` | `Levels/morsecodes.json` | | converted to `MORSE_TABLE` |
| `placeholderPrefab` | `RockPlaceholderPrefab` | | |
| `letterPrefab` | `LetterPrefab` | | |
| `letterContainer` | `Level/letters` (world z -29) | | `LETTERS_POS_*` |
| `rockDragger`, `rockGenerator` | `Player`, `Rocks` | | |
| `visibiltyIndicator` | null | | |

Unity API used: `Start` (`:62`), `Update` (`:374`), `Instantiate(Transform, Vector3, Quaternion)` (`:222`, `:315`), `Destroy(GameObject)` (`:91`, `:297`), `transform.parent` set (`:223`, `:327`), `GetComponent("RockPlaceholder")` by string (`:224`), `GetComponent<TextMesh>()` (`:316`), `GetComponentsInChildren<MeshRenderer>()` and `<TextMesh>()` (`:338`, `:345`), `MeshRenderer.enabled` (`:339`), `TextMesh.text` and `.color` (`:319`, `:324`, `:347`), `Input.GetKeyDown` (`:376`), `Mathf.Max` (`:203`), `string.Split(' ')` (`:301`), JSONObject (`:116`), `Debug.Log` (ignore).

What it does:

1. `Start` builds a `Morse` from the morse text (`:63`).
2. `nextLevel(force)` (`:69`) adds 1 to `currentLevel` (or sets it to `force`). It destroys the old placeholders (`:81`) and renders the level. Returns false if there are no more levels.
3. `renderLevel(i)` (`:114`) reads level `i`. It sets `currentQuestion` and `currentAnswer`. It always rebuilds the decoded letters (`:136`). If `i` is the last index (7) it returns false. If the answer is empty it returns true. Otherwise it encodes the answer, starts a sentence and puts one placeholder per dot or dash.
4. Layout: `sentenceHeight = (numLines + 1) * gap.y` (`:183`). `linePosStart.y = sentenceHeight / 2`, `z` is the Level object's z, which is -4 (`:187`). Each word is a line: `lineWidth = gap.x * numCodes`, `linePosStart.x = -lineWidth / 2 + gap.x / 2`, `y -= gap.y` (`:201` to `:206`). `sentenceSize.x` is the widest line. Gaps between letters take a slot but get no placeholder (`:216`, `:220`).
5. `putPlaceholder` instantiates the prefab at `linePosStart + (U * gap.x, 0, 0)` with identity rotation and sets its parent to the Level object (`:222` to `:227`). It sets `rockDragger` and calls `setDash`.
6. `printDecodedAnswerLetters` (`:294`) destroys old letters and makes one TextMesh per word. The text is each character followed by a space. `yStart = gap.y * words / 2 - 1.394218` (`:303`). Each word is `gap.y` lower than the one before. All letters are at the x of `letters` (0), anchored middle centre. The prefab hides its MeshRenderer.
7. `setDecodedLettersHidden(hidden)` (`:334`) does nothing if the state is unchanged. Otherwise it shows or hides all MeshRenderers below `letters`. It sets the alpha of every TextMesh to 0.5 if `check()` is true, else 0.06. The alpha is chosen at the moment of the switch only.
8. `check()` (`:353`) is false with no placeholders. Otherwise true if every placeholder shows the "correct" sprite.
9. `Update` (`:374`): cheat when `C` and `LeftShift` are both pressed down in the same frame. `cheat()` (`:382`) spawns a rock of the right colour on every placeholder that is not correct, at the placeholder x and y and the new rock's own z.

C file: `level_loader.c`. Needs: spawn, destroy, parent, GetComponentsInChildren equivalents (keep a letters list), TextMesh, key state.

### 2.3 Earth (`Earth.cs`, 222 lines)

Scene: `scenes/Game.json#Earth:Earth`.

| Field | Scene value | Script default | Constant |
|---|---|---|---|
| `telemetryDelay` | 1.8 | 3.5 | `EARTH_TELEMETRY_DELAY` |
| `maxMessageSoundVolume` | 0.3 | 0.4 | `EARTH_MAX_MESSAGE_SOUND_VOLUME` |
| `debugForceNextLevel` | -1 | -1 | `EARTH_DEBUG_FORCE_NEXT_LEVEL` |
| `testIgnoreRoverEnterance` | false | false | never read |
| `couldNotDecode`, `couldDecode`, `newMessage`, `tutorial` | cutscene sprites (section 3) | | |
| `missionUpdateIndicator` | `Rover/updatesIndicator` | | |
| `roverNewMessageSound` | `Rover` AudioSource | | |
| `rocketPrefab` | `RocketPrefab` | | |
| Others | `mars`, `levelLoader`, `roverScreen`, `focusSwitcher` | | |

Private values: `decodedMessageDisplayDuration` 2.0 (`:35`). `didDecodeMark` and `solMark` start at Infinity.

Unity API used: `Start`, `Update`, `Time.time` (`:70`), `Time.deltaTime` (`:117`), `Time.timeScale` set (`:109`, `:190`), `AudioListener.volume` set (`:60`, `:110`, `:191`), `Input.GetKeyDown` for H, Escape, P, Space (`:73`, `:104`), `SpriteRenderer.enabled` and `.color` get and set (`:55` to `:57`, `:113`, `:116` to `:119`), `AudioSource.volume` set (`:114`), `Instantiate(Transform)` (`:219`), `GetComponent<Rocket>()` (`:220`).

What it does:

1. `Start` sets the update indicator alpha to 0, shows the tutorial (pause, mute) and sets the volume back to 1 (`:54` to `:61`). So the first tutorial plays with sound.
2. `newSol()` stores `solMark = Time.time`. Mars calls it whenever the sol number changes.
3. `Update` (`:68`):
   - H, Escape or P: if the tutorial is off, show it. Else dismiss all cutscenes (`:73` to `:80`).
   - While a decoded message displays (`:83`): after 2 s call `endDisplayingDecodedMessage`, else return. `displayingDecodedMessage` is never set back to false, because `continueAfterDecodedDisplay` (`:169`) has no caller. After the first time, `timeMarkBeganDisplayingDecodedMessage` is 0, so the check passes every frame and the end function returns at once (`:156`). No effect.
   - Decode (`:93`): when `t > solMark + telemetryDelay`, Earth is visible, no new level is waiting and Earth is not already answering, call `decode()`.
   - New message (`:99`): when `t > didDecodeMark + telemetryDelay` and no level is waiting, reset the mark and call `onNewMessage()`.
   - Space or forced dismiss (`:104`): hide newMessage, couldDecode and tutorial, set timeScale 1 and volume 1. Space always does this, also when nothing is shown.
   - Update indicator (`:113`): `enabled = willLoadNewLevel`. Rover message sound volume is `maxMessageSoundVolume` while a level waits, else 0. Indicator alpha grows by `deltaTime * 1.2`, back to 0 at 1.
4. `decode()` (`:123`): `check()` of the level loader. If `currentLevel < 0` (before the first message): `earthWillRespond = true`, `didDecodeMark = now`, no check. Else if the answer is right: same plus `beginDisplayingDecodedMessage` (force the map overview for 2 s). Otherwise nothing.
5. `endDisplayingDecodedMessage` (`:154`): clear the overview, `didDecodeMark = now`, show `couldDecode` (pauses the game).
6. `onNewMessage` (`:173`): show `newMessage` (pause), `willLoadNewLevel = true`, `earthWillRespond = false`.
7. `showCutscene(x)` (`:181`): hide `x`, `couldDecode` and `tutorial`, then enable `x`, set `timeScale = 0`, volume 0 and switch the rover screen off. The parameter is named like the field `newMessage`, so the field `newMessage` is never hidden here unless it is `x` (a bug that stays). `couldNotDecode` is never hidden.
8. `playerIsInRover` (`:195`): if a level waits: `nextLevel(debugForceNextLevel)`, then `debugForceNextLevel = -1`. If it returned false (last level) call `beginLastLevel`. Clear `willLoadNewLevel`.
9. `beginLastLevel` (`:218`) instantiates the rocket prefab at its saved position.

C file: `earth.c`.

### 2.4 Mars (`Mars.cs`, 185 lines)

Scene: `scenes/Game.json#Mars:Mars`.

| Field | Scene value | Constant |
|---|---|---|
| `tileSize` | 5 | `MARS_TILE_SIZE` |
| `numTilesX`, `numTilesY` | 66, 44 | `MARS_NUM_TILES_X`, `_Y` |
| `timeScale` | 2000 | `MARS_TIME_SCALE` |
| `secondsSpent` | 0 | |
| `sprites` | 12 entries: `marsTiles_1` x9, `marsTiles_2` x2, `marsTiles_0` x1 | `MARS_TILE_SPRITES` |
| `tileTemplate` | `MarsTile` prefab (scale 5) | |
| `solCounterText` | `Mars/solCounter` GUIText | `SOL_COUNTER_*` |
| `worldBunds`, `darkness`, `currentSol`, `isEarthVisible`, `timeFrozen` | runtime values, all 0 | |

Private: `SOL_IN_HOURS = 24 * 1.027` (`:36`), `solCounterDuration` 3 (`:56`), `timeOffset` 0 (`:69`).

Unity API used: `Start`, `Update`, `Time.time`, `Time.deltaTime`, `Instantiate(Transform, Vector2-as-Vector3, Quaternion)` (`:112`), `GetComponent("SpriteRenderer")` (`:118`), `SpriteRenderer.sprite` set (`:121`), `Sprite.bounds.size.x` (`:79`), `Random.Range(int, int)` (`:120`), `Mathf.Round`, `Mathf.Ceil`, `Mathf.Abs`, `GUIText.text` and `.enabled` (`:158`, `:169`, `:177`), `transform.parent`, `transform.localScale`.

What it does:

1. `Start` (`:61`): `SOL_IN_SECONDS = 24.648 * 3600 = 88,732.8`. Tile size in world units is `sprite width 0.16 * 5 = 0.8`. The grid is 66 x 44. `finalSize = 0.8 * (66 - 1), 0.8 * (44 - 1) = 52 x 34.4`. `topLeft = -finalSize / 2 = (-26, -17.2)`. `worldBunds = (x -26, y -17.2, width 52.8, height 35.2)`. It generates the tiles and calls `rockGenerator.Generate(worldBunds)`.
2. `GenerateJob` (`:105`): rows from the bottom left, left to right then up. Each tile is instantiated at `(x, y, 0)`, parented to Mars, gets a random sprite from the 12 entries, local scale (5, 5, 1) (`:123`).
3. `Update` (`:140`): `relativeTime = Time.time - timeSceneEntered - timePastWhenFrozen`. When not frozen, `secondsSpent = relativeTime * 2000`. When frozen, `timePastWhenFrozen += deltaTime` (so frozen time is cut out). `percentTimeLeft = 1 - (secondsSpent mod SOL) / SOL`. `currentSol = ceil(secondsSpent / SOL)`. So sol 0 lasts only until the first moving frame. Real length of one sol: 88,732.8 / 2000 = 44.3664 s.
4. `darkness = abs(((1 - percentTimeLeft) - 0.5) / 0.5)`, which is `abs(2f - 1)` with `f` the part of the sol that has passed. `isEarthVisible = darkness <= 0.5`, so Earth is visible when `f` is between 0.25 and 0.75.
5. Sol counter (`:158`): text is `"SOL " + currentSol + "\n" + roverScreen.lifeSupport + " LEFT"`. It is shown for 3 s after `newSol()` (`:164` to `:182`), measured with `Time.time`. At the first frame in a fresh game `lastTimeSolCounterDisplayed` is 0, so the counter hides itself unless `Time.time < 3`.
6. On a new sol, `earth.newSol()` is called.

C file: `mars.c`.

### 2.5 RockDragger (`RockDragger.cs`, 161 lines)

Scene: `scenes/Game.json#Player:RockDragger`. Fields: `strainSFX` (the Player's own AudioSource), `dragTargetIndicator` (`RockDragTarget`'s SnapToTarget).

Unity API used: `Update`, `OnTriggerEnter2D` (`:93`), `OnTriggerExit2D` (`:146`), `Input.GetKeyDown("space")` (`:26`), `GetKeyUp("space")` (`:36`), `GetComponent<Rock>()` on a Collider2D (`:94`), `GetComponent<DistanceJoint2D>()` (`:73`), `DistanceJoint2D.connectedBody` set (`:75`), `.enabled` set (`:77`, `:85`), `GetComponent<Rigidbody2D>().velocity` set (`:89`), `AudioSource.Play()` (`:79`), `Vector2.Distance` (`:61`), a `List<Collider2D>`, comparing a destroyed collider with null (`:57`).

What it does:

1. The trigger circle of the player (radius 1.0) adds a Rock collider to `pullCandidates` on enter and removes it on exit. Candidates are added without a duplicate check.
2. Every `Update`: `pullCandidate` = closest live candidate (`:51`). Destroyed ones read as null and are skipped.
3. Space down: if there is a candidate and no active joint, `startPulling` (`:71`). It sets the rock's joint `connectedBody` to the Player body, stores it as `activeJoint`, enables it, plays the strain sound (`RockDragger.cs:79`). That sound is probably silent. The Player's AudioSource is disabled in the scene (`scenes/Game.json#Player:AudioSource:m_Enabled` is 0) and no script enables it (`TopDownControls` only switches the footsteps source). `Play()` on a disabled source plays nothing. Confirmed on the 2016 WebGL build, which logs "Can not play a disabled audio source" at every pull (section 11).
4. Space up: if a joint is active, `release` (`:84`). It disables the joint, clears `connectedBody`, clears `activeJoint`, sets the player velocity to zero.
5. The drag target marker follows the active rock, else the candidate, else nothing (`:42` to `:48`).
6. `releaseIfDragging` (`:139`) has no caller.

C file: `rock_dragger.c`.

### 2.6 Rock (`Rock.cs`, 148 lines)

Prefab: `prefabs/RockPrefab.json#RockPrefab:Rock`. Fields: `darkSprite` (`bis_rock_dark_v3`), `lightSprite` (`bis_rock_light_v3`), `rockShadowPrefab`. `generator` and `shadow` are set at runtime. Private: `sfxMinVelocity` 0.2.

Unity API used: `Start`, `Update`, `OnCollisionEnter2D` (`:81`), `OnCollisionStay2D` (`:93`), `OnTriggerStay2D` (`:99`), `OnTriggerEnter2D` (`:105`), `Collision2D.gameObject.tag`, `Collider2D.tag`, `GetComponent<Rigidbody2D>().velocity` get and set (`:54`, `:119`), `AddTorque` (`:67`), `GetComponent("AudioSource")` (`:33`), `AudioSource.enabled` set (`:60`, `:70`), `Instantiate` (`:34`), `Destroy` (`:130`), `GetComponent<DistanceJoint2D>`, `connectedBody`, `transform.parent.GetComponent<RockGenerator>()` (`:132`).

What it does:

1. `Dark` setter (`:19`) picks the sprite and stores the flag.
2. `Start`: finds the drag AudioSource and instantiates a shadow at the rock position with the same parent (`:34`, `:35`).
3. `Update` (`:52`): if either velocity component is at least 0.2 in size, enable the drag sound (it plays from the start only when the flag goes from off to on) and add torque `-13 * t`, where `t` is the x velocity, or `-vy` when `abs(vy) > abs(vx)` (`:61` to `:67`). Else disable the drag sound. Then place the shadow at `(x, y - 0.3, -0.6)`.
4. Any of the four physics callbacks with a `Rover` tag on the other object calls `relocate()`. Callbacks arrive on enter and stay.
5. `relocate` (`:117`): zero velocity. If the joint has a connected body: clear the dragger's `activeJoint`, set `connectedBody` to null. The joint is not disabled. Destroy the shadow. Ask the generator to replace this rock.

C file: `rock.c`.

### 2.7 RoverScreen (`RoverScreen.cs`, 130 lines)

Scene: `scenes/Game.json#RoverScreen:RoverScreen`. Fields: `startingLifeSupport` 14 (script default 10), `screenGUIText` (13 pt, green, off), `visibilityIndicator` (the `Earth/visibiltyIndicator` GUIText), `levelLoader`, `mars`, `earth`. Constants: `ROVER_SCREEN_STARTING_LIFE_SUPPORT`. The GUITexture on the same object is the background (`ROVER_SCREEN_BACKGROUND_*`).

Private: `maxCharsPerLine` 31, `numLineCapacity` 12.

Unity API used: `Start`, `Update`, `GetComponent<GUITexture>()` (`:32`), `GUITexture.enabled`, `GUIText.text` and `.enabled`, `SceneManager.LoadScene("Death")` (`:113`), `Mathf.Round`.

What it does:

1. `Start`: `startingLifeSupport += 1` makes it 15. `lifeSupport = 15`.
2. `printScreen` (`:42`) builds the text every `Update` (and once in `Start`):
   - `"EARTH:\n------\n" + currentQuestion`.
   - Wrapping (`:54` to `:66`): a new line after every 31 characters of that text, counting the newlines already in it. The `\n` characters of the header and of the question count as normal characters. `lineCount` starts at 3 and goes up by 1 per inserted newline.
   - Then `12 - lineCount` blank lines, then 31 underscores, a blank line, `"LIFE SUPPORT LEFT:      " + two-digit life support + " SOLS"`.
   - `"HOURS UNTIL NEXT SOL:     " + hh:mm`, with `hh = (int) hoursUntilNextSol` and `mm = (int) ((secondsUntilNextSol / 60) mod 60)`, padded to two digits.
   - `"SATELITE IMAGERY COVERAGE:  " + ("YES" or " NO")`.
   - Side effect: `visibilityIndicator.enabled = !isEarthVisible` and its text is `"VISIBILITY IS TOO LOW\n" + round((1 - darkness + 0.5) * 100) + "%, WAIT FOR SOL"`. When the indicator is shown, `darkness > 0.5`, so the number runs from 50 to 100. The line `if (formattedDarkess.Length < 2) d1 = ...` (`:97`) pads the wrong variable. It never fires, because the number has at least 2 digits.
3. `Update` (`:104`): sets the screen text, `lifeSupport = startingLifeSupport - mars.currentSol`, and loads `Death` when `lifeSupport <= -1` (`:111`). That is `currentSol >= 16`, which is 15 sols = 665.5 s of unfrozen game time.
4. `setOn(on)` (`:117`): ignore if unchanged. Set the background and text visible. When switched on, call `earth.playerIsInRover()`.

C file: `rover_screen.c`.

### 2.8 RockPlaceholder (`RockPlaceholder.cs`, 113 lines)

Prefab: `prefabs/RockPlaceholderPrefab.json#RockPlaceholderPrefab:RockPlaceholder`. Fields: `dashSprite`, `dotSprite`, `incorrectSprite` (`bis_morse_wrong_v1`), `correctSprite` (`bis_morse_correct_v1`). `rockDragger`, `rocks`, `snappedRock` are null. Private: `clearTime` 0.6.

Unity API used: `Start` (`Time.time` at `:28`), `OnTriggerEnter2D` (`:52`), `OnTriggerExit2D` (`:98`), `GetComponent<Rock>()`, `GetComponent<AudioSource>()`, `AudioSource.Play()` (`:65`), `SpriteRenderer.sprite` get and set, `Rock.relocate()`.

What it does:

1. `setDash(b)` stores the type and sets the dash or dot sprite.
2. Rock enters (`:52`): if the placeholder is younger than 0.6 s, relocate the rock and stop. Else if `rock.Dark == isDash` snap (store the rock), play the sound, show the correct sprite. Else if no rock is snapped, show the incorrect sprite.
3. Rock leaves (`:98`): if it is the snapped rock, clear it and reset the sprite to dash or dot. Else if nothing is snapped, reset the sprite.
4. `isCorrect()` is true if the current sprite is the correct sprite. If `setDash` was never called, `spriteRenderer` is null and this would throw. It always is called first.

A rock destroyed while inside a placeholder gets no exit event from the game code. Whether Unity 5.4 sends one is still unverified. The port sends one at destroy time (section 11).

C file: `rock_placeholder.c`.

### 2.9 CamerFocusSwitcher (`CamerFocusSwitcher.cs`, 110 lines)

Scene: `scenes/Game.json#Main Camera:CamerFocusSwitcher`. Fields: `levelLoader`, `playerTransform`, `roverForcusTarget` (`Rover/roverFocusTarget`, world (4.76, 4.5)), `nightosphere` (MeshRenderer of the overlay), `mars`, `playerIsInRover` false.

Unity API used: `Start`, `Update`, `Input.GetKey("m")` (`:46`), `Camera.main.orthographicSize` get and set (`:33`, `:71`), `Camera.main.GetComponent<Camera>()`, `Screen.width`, `Screen.height` (`:86`, `:104`), `renderer.material.color` get and set (`:62`, `:64`), `iTween.MoveUpdate` (`:70`), `Mathf.Clamp`, `Mathf.Max`.

What it does, every `Update`:

1. If `M` is held or the overview is forced: target = the Level object position (0, 0), camera size = `levelFocusCameraSize()`, speed 1.0, letters shown. Else if the player is in the rover: target = `roverFocusTarget`, speed 1.0. Else target = the player, speed 0.2.
2. `levelFocusCameraSize` (`:101`): `p = sentenceSize.x / horzExtent` where `horzExtent = size * Screen.width / Screen.height`. `p = max(p, 0.9)`. Result = `3.6 * p`. It uses the current camera size, so the target changes while the camera zooms. With `W = sentenceSize.x` it settles at `size = sqrt(2.4 * W)` when that is above 3.24, else at 3.24 (my derivation from `:101` to `:108`, not measured).
3. Letters hidden or shown through the level loader (`:59`).
4. Night overlay: `a += (darkness * 0.85 - a) / 5` per frame (`:62` to `:64`). The material starts with alpha 0.2157.
5. Clamp the target (`:83`): `rightBound = worldBunds.width / 2 - horzExtent`, `topBound = worldBunds.height / 2 - vertExtent`. The camera z stays.
6. `speed >= 0` is always true. `iTween.MoveUpdate(camera, target, speed)` then `size += (targetSize - size) / 10` per frame.

`iTween.MoveUpdate` (`iTween.cs:5297`, `:5205`) is not a lerp. It calls `Mathf.SmoothDamp` on x, y and z with a new zero velocity each call, `smoothTime = speed * 0.05` (`Defaults.updateTimePercentage` is 0.05, `iTween.cs:199`) and the default `Time.deltaTime`. Speed 0.2 means 0.01 s. Speed 1.0 means 0.05 s. At `deltaTime = 0` (game paused) `SmoothDamp` returns the current position, so the camera does not move while the game is paused. The camera starts at (-1000, -1000, -30), and the game starts paused with the tutorial, so the first picture is the tutorial over an empty background (section 9, item 11).

C file: `camera_focus.c`. `CutsSceneScaleFixer` (`:16`, in `LateUpdate`) sets the scale of `cutscenes` to `defaultScale * camera.size / defaultCameraSize` on all three axes, with `defaultScale = 1.7` and `defaultCameraSize = 3.6` read in `Start`. The camera size changes only in the overview, so the cutscene images shrink or grow with the zoom.

### 2.10 Morse (`Morse.cs`, 95 lines)

Plain class, not a component. Codes: gap 0, dot 1, dash 2.

- `encode(sentence)` (`:29`): empty gives no words. Split on space. For each word, for each letter add the code and a gap, then drop the last gap. Unknown letters give an empty code but still add a gap.
- `encodeChar` (`:66`): linear search of the table for an entry whose first or second field equals the letter. The table has the upper case letter first and the lower case second.

Unity API used: none except the JSONObject parser. C file: `morse.c`.

### 2.11 TopDownControls (`TopDownControls.cs`, 89 lines)

Scene: `scenes/Game.json#Player:TopDownControls`. Fields: `audioListener` (`Player/pivot`), `footStepsSFX` (`Player/footsteps`), `animator`, `mars`.

Private: `Agility` 10 and `Speed` 10 give `walkSpeed = (10 + 10 / 5) = 12`, then `* 0.6 = 7.2` (`:26` to `:32`). `sprintSpeed` is never used. `sfxMinVelocity` 0.5.

Unity API used: `FixedUpdate` (`:37`), `Input.GetAxis("Horizontal")` and `"Vertical"` (`:43`), `Mathf.Lerp(0, a * speed, 0.8)` (`:43`), `Rigidbody2D.velocity` get and set, `GetComponent("RockDragger")`, `AudioSource.pitch` set (`:49`, `:52`), `AudioSource.enabled` set (`:61`, `:76`), `transform.rotation` get and set with the `y` component written directly (`:63` to `:69`), `Animator.Play(string)` (`:72`, `:78`), `transform.position` get and set (`:84`, `:87`), `Mathf.Clamp`, `Mathf.Abs`.

What it does, every fixed update (50 Hz):

1. Velocity = `(0.8 * axisX * 7.2, 0.8 * axisY * 7.2)`. The top speed is 5.76 units/s. The axis uses the smoothing in section 7.
2. Footstep pitch is 0.9 while dragging, else 1.9.
3. If either velocity component has size 0.5 or more: footsteps on, the player rotation `y` is set to 180 when `vx <= 0` else 0, the listener pivot gets the opposite, and the animator plays `drag` or `run`. Otherwise footsteps off and the animator plays `idle`.
4. The position is clamped to `x` in `±(52.8 / 2 - 0.5) = ±25.9` and `y` in `±(35.2 / 2 - 0.8) = ±16.8`.

The code sets a raw quaternion component to 180 or 0, not an angle (`r.y = 180`). The effect is a half turn around the Y axis (the sprite and its children flip). Unity normalises that quaternion into a half turn around Y (179.4 degrees). The listener's forward measured on the WebGL build agrees. The port mirrors the body polygon while the player faces left, which follows Unity's documented collider geometry. That part was not measured (section 11).

C file: `player.c`.

### 2.12 RockGenerator (`RockGenerator.cs`, 76 lines)

Scene: `scenes/Game.json#Rocks:RockGenerator`. Fields: `numRocks` 100 (script default 40), `rockPrefab`, `rockShatterAnimationPrefab`, `rocks` (empty list). Object at z -3.

Unity API used: `Instantiate(Transform, Vector3, Quaternion)` (`:40`, `:64`), `Random.Range(int, int)` (`:29`), `Random.Range(float, float)` (`:55`), `Destroy` (`:66`), `transform.parent` set (`:43`), `List<Rock>`.

What it does:

1. `Generate(bounds)`: `numRocks` times, pick dark or light with `Random.Range(0, 2) == 0`, add a rock.
2. `addRock(dark)` (`:37`): position random in `±(bounds.width / 2 * 0.5)` and `±(bounds.height / 2 * 0.5)` which is `±13.2` by `±8.8`, with z equal to the generator's z (-3). It sets the dark flag, the parent and `generator`.
3. `replace(rock)` (`:62`): instantiate the shatter prefab at the rock position, destroy the rock, `numRocks++`, call `Generate` (which then makes exactly one rock, if `numRocks` was 0).

`Start` makes a new empty list (`:20`). `Mars.Start` may call `Generate` before or after that. Both orders work, because `rocks` is already an empty list from the scene. Nothing reads `rocks`.

C file: `rock_generator.c`.

### 2.13 RoverInsideTrigger, Rover, VisibleOnTrigger

`RoverInsideTrigger` (`RoverInsideTrigger.cs`, 59 lines) sits on `Rover/playerInsideRoverTrigger`. Fields: `setOnPitch` 0.6, `setOffPitch` 0.9, `audioSource` (its own AudioSource, Menu1.wav, volume 0.143, pitch -0.88), `focusSwitcher`, `roverScreen`, `mars`.

- `OnTriggerEnter2D(o)` (`:26`): if `o.tag == "Player"` and the camera switcher says the player is not in the rover: stop the sound, set pitch to `setOnPitch`, play it, set `playerIsInRover = true`, `roverScreen.setOn(true)`, `mars.timeFrozen = true`.
- `OnTriggerExit2D(o)` (`:42`): the same in reverse: pitch `setOffPitch`, `setOn(false)`, `timeFrozen = false`.
- Both colliders of the player carry the tag `Player`, so the large trigger circle alone can turn the screen on. When one collider leaves, the screen turns off even if the other is still inside (section 5).

`Rover` (`Rover.cs`, 35 lines) is on `Rover/RoverBody`. Field `roof` = the roof sprite. On trigger enter or exit with `o.tag == "Player" && o.isTrigger == false` it shows or hides the roof (`:20`, `:29`). `isTrigger == false` picks the player's body polygon.

`VisibleOnTrigger` (35 lines) does the same for its own SpriteRenderer. It sits on `RocketPrefab/RocketCover`.

C file: `rover.c`.

### 2.14 SnapToTarget (`SnapToTarget.cs`, 41 lines)

Scene: `scenes/Game.json#RockDragTarget:SnapToTarget`. Fields: `dashTarget` (`rockDragTarget_dash`), `dotTarget` (`rockDragTarget`), `target` (set by RockDragger). Every `Update`: if there is a target, `isDash = target Rock.Dark`, move to the target x and y (z stays -5), show, and set the sprite. Else hide. The marker colour is white with alpha 0.353 in the scene (prefab 0.627). Unity API used: `GetComponent<Renderer>().enabled`, `(SpriteRenderer) GetComponent<Renderer>()`, `sprite`. C file: `drag_target.c`.

### 2.15 InsideRocketTrigger, Rocket

`InsideRocketTrigger.OnTriggerEnter2D` (`InsideRocketTrigger.cs:16`): if `o.tag == "Player"`, call `transform.parent.GetComponent<Rocket>().playerJustGitInRocket()`. `Rocket.playerJustGitInRocket` (`Rocket.cs:19`) loads `Win`. Both Player colliders count, so touching the small `InsideTrigger` box (0.31 x 0.16) with either one wins. C file: `rocket.c`.

### 2.16 Title, GUITextButton

`Title` (`Title.cs`, 34 lines) is on the Title camera. `Awake` (`:15`) subscribes `onOpenSDSite` and `onPlay` to the buttons' `OnButtonMouseUp` events. `onOpenSDSite` calls `Application.OpenURL("http://superdamage.com")`. `onPlay` loads `Story1`.

`GUITextButton` (`GUITextButton.cs`, 32 lines) is on `PLAY` and `SD`. Fields: `highlightedColor` red (1, 0, 0, 1), `normalColor` white, `higlightSFX` (`highlightSound`, volume 0.177), `clickSFX` (`clickSound`, volume 0.462), `identifier` "". `OnMouseUp` plays the click sound and fires the event. `OnMouseEnter` plays the highlight sound and sets the GUIText colour to the highlight colour. `OnMouseExit` sets it to the normal colour. Unity calls these for GUIText through its hit test. `OnMouseUp` goes to the element that got the mouse down, wherever the button is released (Unity documentation).

Unity API used: `OnMouseUp`, `OnMouseEnter`, `OnMouseExit`, `GetComponent<GUIText>().color`, `AudioSource.Play()`, delegates and events, `SceneManager.LoadScene(string)`, `Application.OpenURL`. C file: `title.c`.

### 2.17 Intro, NextSceneOnSpace, EndGame

- `Intro` (`Intro.cs`, 26 lines), Intro scene, camera object. `Start` hides the logo renderer. `Update` subtracts `Time.deltaTime` from 2.0. At or below 1.9 the logo shows (`:18`). At or below 0 it loads `Title`.
- `NextSceneOnSpace` (`NextSceneOnSpace.cs`, 31 lines), on `gfx` in Story1 and Story2. Fields: `dismissDuration` 20, `nextScene` "Story2" and "Game". Loads the next scene when `Time.time >= timeSceneEntered + 20`, or on Space down or left mouse down (`:22`).
- `EndGame` (`EndGame.cs`, 31 lines), on `gfx` in Death and Win. Same, with a fixed 5 s. Loads scene index 0 (Intro).

C file: `scenes.c`. Unity API used: `Time.time`, `Time.deltaTime`, `Input.GetKeyDown("space")`, `Input.GetMouseButtonDown(0)`, `SceneManager.LoadScene(string or int)`, `Renderer.enabled`.

### 2.18 TimedObjectDestructor.js (Standard Assets, 14 lines)

On `RockShatterPrefab` (`Assets/Standard Assets/Particles/Legacy Particles/TimedObjectDestructor.js`). Fields: `timeOut` 2.0 (script default 1.0), `detachChildren` false. `Awake` calls `Invoke("DestroyNow", timeOut)`. `DestroyNow` destroys the game object (`DestroyObject`). Unity API used: `Invoke`, `DestroyObject`, `transform.DetachChildren` (not reached). C file: `effects.c`.

### 2.19 CameraFollowBounds

Not attached anywhere (`CameraFollowBounds.cs`, 63 lines). Leave it out.

### 2.20 Third-party code

- iTween (`Assets/iTween/Plugins/iTween.cs`, 7,500 lines): one call, `iTween.MoveUpdate(GameObject, Vector3, float)` at `CamerFocusSwitcher.cs:70`. It builds a Hashtable and calls the 2-argument overload (`iTween.cs:5205`). The math is in section 2.9. The Rigidbody branch (`:5280`) does not apply: the camera has no 3D Rigidbody.
- JSONObject (`Assets/JSON/JSONObject.cs`): used at `LevelLoader.cs:116` (`new JSONObject(text).list`, `["question"].str`) and `Morse.cs:23`, `:72`. Quirk: string values are cut out of the text with `Substring` and not unescaped (`JSONObject.cs:262`). So in `morsecodes.json` the alternate character of `DOUBLE_QUOTE` is the two characters `\"` and a real quote can never be encoded. The port tables keep this (`tables.h`).

### 2.21 UnityEngine code the port must copy

Source of the text: `ilspycmd` 11.1.0.9782 decompiling `RockMorse.app/Contents/Resources/Data/Managed/UnityEngine.dll` (the Unity 5.4.3f1 Mac build). Command: `DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec ilspycmd -t UnityEngine.Mathf -o <dir> UnityEngine.dll`. The code below is quoted from that output without change (only the leading tab removed). Full output: `extract/decompiled/UnityEngine/Mathf.cs`, `Vector2.cs` and `Random.cs`. Only methods that the game or the iTween code it calls reaches are quoted. Doc comments and attributes other than the two `[ExcludeFromDocs]`/`[DefaultValue]` ones are left out here.

`Mathf.SmoothDamp`, reached from `iTween.MoveUpdate` (`iTween.cs:5256` to `:5258`, called as `Mathf.SmoothDamp(current, target, ref velocity, time)` for x, y and z with a fresh `velocity` of 0 each call). The four-argument overload runs, which calls the six-argument one. The five-argument overload is not reached.

```csharp
[ExcludeFromDocs]
public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime)
{
	float deltaTime = Time.deltaTime;
	float maxSpeed = float.PositiveInfinity;
	return SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, deltaTime);
}

public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime, [DefaultValue("Mathf.Infinity")] float maxSpeed, [DefaultValue("Time.deltaTime")] float deltaTime)
{
	smoothTime = Max(0.0001f, smoothTime);
	float num = 2f / smoothTime;
	float num2 = num * deltaTime;
	float num3 = 1f / (1f + num2 + 0.48f * num2 * num2 + 0.235f * num2 * num2 * num2);
	float value = current - target;
	float num4 = target;
	float num5 = maxSpeed * smoothTime;
	value = Clamp(value, 0f - num5, num5);
	target = current - value;
	float num6 = (currentVelocity + num * value) * deltaTime;
	currentVelocity = (currentVelocity - num * num6) * num3;
	float num7 = target + (value + num6) * num3;
	if (num4 - current > 0f == num7 > num4)
	{
		num7 = num4;
		currentVelocity = (num7 - num4) / deltaTime;
	}
	return num7;
}
```

Notes on this code:

- `smoothTime` is `speed * 0.05` (0.01 or 0.05 s), see 2.9. The `Max(0.0001f, ...)` guard never triggers.
- `maxSpeed` is `float.PositiveInfinity`, so `num5` is infinity and `Clamp` leaves `value` unchanged.
- At `deltaTime` 0: `num2` is 0, `num3` is 1, `num6` is 0, so the result is `target + value`, which is `current`. The camera does not move. If `current` equals `target` exactly, the last `if` fires and `currentVelocity` becomes 0/0 = NaN. The game throws that velocity away, so it has no effect.
- Computed in float32 throughout. The port should use `float` and the same order of operations. A fused multiply-add (FMA) can change the last bit, so compile with `-ffp-contract=off`.

Other `Mathf` and `Vector2` code the game reaches. Call sites: `Lerp` `TopDownControls.cs:43` and `:44` (t is 0.8, so the result is `0.8 * a`), `Clamp` `TopDownControls.cs:85`, `:86`, `CamerFocusSwitcher.cs:95`, `:96`, `Max` `LevelLoader.cs:203`, `CamerFocusSwitcher.cs:106`, `Abs` `Mars.cs:161`, `TopDownControls.cs:58`, `Rock.cs:57`, `Round` `Mars.cs:120`, `RoverScreen.cs:96`, `Ceil` `Mars.cs:156`, `Vector2.Distance` `RockDragger.cs:61`, `:107`, `:108`. `Mathf.Infinity` is `float.PositiveInfinity` (`Earth.cs:17`, `:31`, `:100`, `RockDragger.cs:53`).

```csharp
public static float Lerp(float a, float b, float t)
{
	return a + (b - a) * Clamp01(t);
}

public static float Clamp01(float value)
{
	if (value < 0f)
	{
		return 0f;
	}
	if (value > 1f)
	{
		return 1f;
	}
	return value;
}

public static float Clamp(float value, float min, float max)
{
	if (value < min)
	{
		value = min;
	}
	else if (value > max)
	{
		value = max;
	}
	return value;
}

public static float Max(float a, float b)
{
	return (!(a > b)) ? b : a;
}

public static float Abs(float f)
{
	return Math.Abs(f);
}

public static float Round(float f)
{
	return (float)Math.Round(f);
}

public static float Ceil(float f)
{
	return (float)Math.Ceiling(f);
}
```

```csharp
public static float Distance(Vector2 a, Vector2 b)
{
	return (a - b).magnitude;
}

public static Vector2 operator -(Vector2 a, Vector2 b)
{
	return new Vector2(a.x - b.x, a.y - b.y);
}

public static implicit operator Vector2(Vector3 v)
{
	return new Vector2(v.x, v.y);
}

public float magnitude => Mathf.Sqrt(x * x + y * y);
```

- `Mathf.Round` is `Math.Round(double)` with the default rounding, which is half to even: 0.5 gives 0, 1.5 gives 2, 2.5 gives 2. In C use `nearbyintf` with the default rounding mode, or `rintf`.
- `Vector2 pos = transform.position` (`RockDragger.cs:59`, `:60`, `:103` to `:105`) drops z, so `Distance` is the distance in the xy plane.
- `Random.Range(int, int)` and `Random.Range(float, float)` are not managed code (`Random.cs`: `RandomRangeInt` and `Range(float, float)` are `extern`). The int version has an exclusive upper bound and the float version an inclusive one (the doc comments in `Random.cs`). The generator itself is native, so the port cannot copy its number stream.
- `Color.red` is (1, 0, 0, 1) and `Color.white` is (1, 1, 1, 1) (`GUITextButton.cs:9`, `:10`).


## 3. Scenes

Build order (`settings/EditorBuildSettings.json`): 0 Intro, 1 Title, 2 Story1, 3 Story2, 4 Game, 5 Death, 6 Win. Window 660 x 440, not resizable, not full screen (`WINDOW_WIDTH`, `WINDOW_HEIGHT`).

Flow: Intro (2 s) to Title. Title Play to Story1. Story1 (Space, click or 20 s) to Story2 (same) to Game. Game to Death when life support runs out, to Win in the rocket. Death and Win return to Intro after 5 s, Space or a click.

Each line: object, world position, world scale, then the components. `order` is the sprite sorting order (always 0), so draw order comes from z (section 7). "off" means the component is disabled at the start. Objects marked inactive are inactive at the start.

### 3.1 Intro (`scenes/Intro.json`)

Camera background black (0, 0, 0, 0.0196), ortho size 5.

- `superdamage` world pos (0, 0, 0) scale (1.88491, 1.88491) SpriteRenderer[superdamage, order 0, off]
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener; Intro
- `Explosion_04` world pos (0, 0, 0) scale (1, 1) AudioSource[Explosion_04.wav, vol 0.538, pitch 1.06, play on awake, on]

### 3.2 Title (`scenes/Title.json`)

Camera as in Intro. GUIText anchor 4 is middle centre, align 1 is centre. All text uses `Joystix/joystix monospace.ttf`. Positions are viewport fractions.

- `clickSound` world pos (0, 0, 0) scale (1, 1) AudioSource[HitsAccept2.wav, vol 0.462, pitch 1, on]
- `highlightSound` world pos (0, 0, 0) scale (1, 1) AudioSource[GlassHitScroll6.wav, vol 0.177, pitch 1, on]
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener; Title
- `PLAY` world pos (0.5, 0.2, 0) scale (1, 1) GUIText["PLAY", size 50, anchor 4, align 1, rgba 255,255,255,255, on]; GUITextButton
- `WR` world pos (0.5, 0.54, 0) scale (1, 1) GUIText["WE ARE", size 12, anchor 4, align 1, rgba 255,255,255,255, on]
- `Title` world pos (0.5, 0.8, 0) scale (1, 1) GUIText["ROCKMORSE", size 40, anchor 4, align 1, rgba 255,255,255,255, on]
- `LD` world pos (0.5, 0.72, 0) scale (1, 1) GUIText["Made For Ludum Dare 30", size 12, anchor 4, align 1, rgba 255,255,255,255, on]
- `SD` world pos (0.5, 0.48, 0) scale (1, 1) GUIText["SUPER DAMAGE", size 17, anchor 4, align 1, rgba 255,255,255,255, on]; GUITextButton

### 3.3 Story1 and Story2

Camera background (0.192, 0.302, 0.475, 0.0196), size 5. The GUITexture scale is 0, so the rectangle is the pixel inset: 660 x 440 at (0, 0). Colour (0.5, 0.5, 0.5, 0.5) means no tint.

- `gfx` world pos (0, 0, 0) scale (0, 0) GUITexture[Sprites/Slides/rm_tutorial_v4_1.png, inset 660x440 at (0,0), color 0.5, on]; NextSceneOnSpace
- `Menu4` world pos (0, 0, 0) scale (1, 1) AudioSource[Menu4.wav, vol 1, pitch 1, play on awake, on]
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener

- `Menu4` world pos (0, 0, 0) scale (1, 1) AudioSource[Menu4.wav, vol 1, pitch 1, play on awake, on]
- `gfx` world pos (0, 0, 0) scale (0, 0) GUITexture[Sprites/Slides/rm_tutorial_v4_2.png, inset 660x440 at (0,0), color 0.5, on]; NextSceneOnSpace
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener

### 3.4 Game (`scenes/Game.json`)

Camera background (0.408, 0.298, 0.204, 0.0196), clear flags solid colour, ortho size 3.6, near 0.3, far 40, at (-1000, -1000, -30). There is no AudioListener on the camera. The listener is on `Player/pivot/audioListener`. Ambient light is (1, 0.671, 0.493) (`RenderSettings`), no fog, no lights.

Active at the start: everything except `Level/rockDestructionEffect`. Nothing in any script activates that object (an inactive instance of the Standard Assets Fireworks prefab with a Fairy Dust material). Disabled components at the start are listed in the tree.

- `Level` world pos (0, 0, -4) scale (1, 1) LevelLoader
  - `rockDestructionEffect` **inactive** world pos (0, 0, -4) scale (1, 1) EllipsoidParticleEmitter; ParticleAnimator; ParticleRenderer
  - `letters` world pos (0, 0, -29) scale (1, 1) 
- `Mars` world pos (0, 0, 0) scale (1, 1) Mars
  - `solCounter` world pos (0.5, 0.2, 0) scale (1, 1) GUIText["SOL 1", size 30, anchor 4, align 1, rgba 255,255,255,49, off]
- `RoverScreen` world pos (0.5, 0.5, -30) scale (1, 1) GUITexture[Sprites/bis_screen_v2.png, inset -220x0 at (0,0), color 0.5, off]; RoverScreen
  - `screenGUIText` world pos (0.07, 0.91, 0) scale (1, 1) GUIText["", size 13, anchor 0, align 0, rgba 47,208,26,255, off]
- `Main Camera` tag MainCamera world pos (-1000, -1000, -30) scale (1, 1) Camera[ortho True, size 3.6]; GUILayer; FlareLayer; CamerFocusSwitcher
  - `cutscenes` world pos (-1000, -1000, -29) scale (1.7, 1.7) CutsSceneScaleFixer
    - `spr_couldNotDecode` world pos (-1000, -1000, -29) scale (1.7, 1.7) SpriteRenderer[bis_cutscene_v1_1, order 0, off]
    - `spr_newMessage` world pos (-1000, -1000, -29) scale (1.7, 1.7) SpriteRenderer[bis_cutscene_v1_3, order 0, off]
    - `spr_couldDecode` world pos (-1000, -1000, -29) scale (1.7, 1.7) SpriteRenderer[bis_cutscene_v1_2, order 0, off]
    - `spr_tutorial` world pos (-1000, -1000, -29) scale (1.7, 1.7) SpriteRenderer[rm_tutorial_v4_3, order 0, off]
  - `nightosphere` world pos (-1000, -1000, -12) scale (6, 1, 6) rot euler (270, 0, 0) MeshFilter; MeshCollider; MeshRenderer
  - `CameraFocus` world pos (-1000, -1000, -30) scale (1, 1) 
- `Menu4` world pos (0, 0, 0) scale (1, 1) AudioSource[Menu4.wav, vol 1, pitch 1, play on awake, on]
- `RockDragTarget` world pos (-0.633234, -3.23401, -5) scale (4.5, 4.5) SpriteRenderer[rockDragTarget, order 0, off color(1,1,1,0.352941)]; SnapToTarget
- `Earth` world pos (0, 0, 0) scale (1, 1) Earth
  - `visibiltyIndicator` world pos (0.7, 0.99, -25) scale (1, 1) GUIText["WAIT FOR DAYLIGHT IN", size 11, anchor 0, align 2, rgba 255,255,255,134, off]
- `Rocks` world pos (0, 0, -3) scale (1, 1) RockGenerator
- `Player` tag Player world pos (6.56, 2.35, -1) scale (5, 5) TopDownControls; Rigidbody2D[mass 1, drag 0/0.05, dynamic, fixed angle]; RockDragger; CircleCollider2D[r 0.2, trigger]; AudioSource[player_strain.mp3, vol 0.027, pitch 1, off, probably silent: see 2.5]; Animator; SpriteRenderer[bis_dude_ani_Animation 1_3, order 0, on]; PolygonCollider2D[8 points, solid, 3 pieces]
  - `pivot` world pos (6.56, 2.35, -1) scale (0, 5) rot Y 180 (local euler (0, 180, 0))
    - `audioListener` world pos (6.56, 2.35, -1) scale (0, 5) rot Y 180 (world; local euler 0) AudioListener
    - `Sci-fi_AmbienceLoop1` world pos (6.56, 2.35, -1) scale (0, 0) rot Y 180 (world; local euler 0) AudioSource[Sci-fi_AmbienceLoop1.wav, vol 0.354, pitch 1, loop, play on awake, on]
  - `footsteps` world pos (6.56, 2.35, 0) scale (1, 1) AudioSource[Footstep_Gravel_1.wav, vol 0.05, pitch 1, loop, play on awake, off]
- `Rover` world pos (9.3, 4.5, -0.5) scale (1, 1) AudioSource[newMessage.mp3, vol 0, pitch 0.95, loop, play on awake, on]
  - `Reverb` world pos (9.3, 4.5, -0.5) scale (1, 1) AudioReverbZone
  - `RoverBody` tag Rover world pos (9.3, 4.5, -0.5) scale (5, 5) SpriteRenderer[bis_rover_v2, order 0, on]; Rigidbody2D[mass 1, drag 0/0.05, kinematic]; BoxCollider2D[size 0.25x0.267225, trigger]; Rover
  - `bis_rover_v2_roof` tag Rover world pos (9.3, 4.5, -8.5) scale (5, 5) SpriteRenderer[bis_rover_v2_roof, order 0, off]; Rigidbody2D[mass 1, drag 0/0.05, kinematic]; PolygonCollider2D[14 points, solid, 7 pieces]
  - `rockRepeller` tag Rover world pos (9.3, 4.5, -0.5) scale (1, 1) CircleCollider2D[r 3.29, trigger]
  - `roverFocusTarget` world pos (4.76, 4.5, -0.5) scale (1, 1) 
  - `playerInsideRoverTrigger` world pos (8.92976, 4.51146, -0.5) scale (5, 5) CircleCollider2D[r 0.12, trigger]; RoverInsideTrigger; AudioSource[Menu1.wav, vol 0.143, pitch -0.88, on]
    - `chatter` world pos (9.18677, 4.76847, -0.5) scale (5, 5) AudioSource[static.mp3, vol 0.288, pitch 1, loop, play on awake, on]
  - `updatesIndicator` world pos (8.1196, 3.45601, -20.5) scale (2, 2) SpriteRenderer[bis_exclamation_v2_2, order 0, on]

Runtime objects (not in the scene file): 2,904 ground tiles under `Mars` (66 x 44), 100 rocks under `Rocks`, one shadow per rock (sibling of the rock), placeholders under `Level` per message, TextMesh letters under `Level/letters`, the rocket, shatter effects.

### 3.5 Death and Win

Death: one GUITexture, the `Victory_...` jingle (volume 0.35, plays once). Win: one GUITexture, music (loop, volume 1) and a horn (loop, volume 1).

- `Victory_-arewhyae-8093_hifi` world pos (0, 0, 0) scale (1, 1) AudioSource[Victory_-arewhyae-8093_hifi.mp3, vol 0.35, pitch 1, play on awake, on]
- `gfx` world pos (0, 0, 0) scale (0, 0) GUITexture[Sprites/Cutscenes/death.png, inset 660x440 at (0,0), color 0.5, on]; EndGame
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener

- `bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm` world pos (0, 0, 0) scale (1, 1) AudioSource[bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm.mp3, vol 1, pitch 1, loop, play on awake, on]
- `gfx` world pos (0, 0, 0) scale (0, 0) GUITexture[Sprites/Cutscenes/win.png, inset 660x440 at (0,0), color 0.5, on]; EndGame
- `FOLEY HORN PARTY TOY BLOW OUT 01` world pos (0, 0, 0) scale (1, 1) AudioSource[FOLEY HORN PARTY TOY BLOW OUT 01.mp3, vol 1, pitch 1, loop, play on awake, on]
- `Main Camera` tag MainCamera world pos (0, 0, -10) scale (1, 1) Camera[ortho True, size 5]; GUILayer; FlareLayer; AudioListener

## 4. Prefabs

Positions in a prefab are its saved ones. `Instantiate(prefab, position, rotation)` replaces the root position, so the saved position matters only for `RocketPrefab` (instantiated without a position) and for children.

### RockPrefab

Rock (dark or light by script). Root saved position irrelevant.

- `RockPrefab` world pos (-0.634035, -3.23763, 0) scale (4.5, 4.5) SpriteRenderer[bis_rock_dark_v3, order 0, on material cutout.mat (built-in `Sprites/Default`, alpha blended, section 7.5)]; CircleCollider2D[r 0.055, solid]; Rigidbody2D[mass 12, drag 12/12, dynamic]; Rock; DistanceJoint2D[distance 0.55, disabled]; AudioSource[drag.mp3, vol 0.158, pitch 0.45, loop, play on awake, off]

### RockShadowPrefab

Rock shadow, a sibling of its rock.

- `RockShadowPrefab` world pos (0, 0, 0) scale (5, 5) SpriteRenderer[bis_shadow_v1, order 0, on material cutout.mat (built-in `Sprites/Default`, alpha blended, section 7.5)]

### RockPlaceholderPrefab

One dot or dash slot. Colour alpha 0.341.

- `RockPlaceholderPrefab` world pos (0.419964, 3.06142, 0) scale (4.5, 4.5) SpriteRenderer[bis_morse_dash_v1, order 0, on color(1,1,1,0.341176)]; RockPlaceholder; CircleCollider2D[r 0.045, trigger]; AudioSource[Arcade_S-wwwbeat-8526_hifi.mp3, vol 1, pitch 1, on]

### RockDragTarget

Marker. In the scene the colour alpha is 0.353 and the instance is used.

- `RockDragTarget` world pos (-0.633234, -3.23401, -5) scale (4.5, 4.5) SpriteRenderer[rockDragTarget, order 0, off color(1,1,1,0.627451)]; SnapToTarget

### LetterPrefab

One decoded line of text, MeshRenderer off. The stray `Rocket` script is harmless.

- `LetterPrefab` world pos (0, 0, 0) scale (0.1, 0.1) MeshRenderer; TextMesh; Rocket

### MarsTile

One ground tile. Its sprite is set by `Mars`.

- `MarsTile` world pos (0, 0, 0) scale (5, 5) SpriteRenderer[None, order 0, on]

### RocketPrefab

Instantiated at the saved position (-0.39, 1.79, -0.5) when the last message is read.

- `RocketPrefab` tag Rover world pos (-0.389627, 1.78579, -0.5) scale (1, 1) Rocket
  - `RocketBody` tag Rover world pos (-0.389627, 1.78579, -0.5) scale (4.2, 4.2) SpriteRenderer[bis_rocket_v1, order 0, on]; PolygonCollider2D[19 points, solid, 7 pieces]; Rigidbody2D[mass 1, drag 0/0.05, kinematic]
  - `RocketCover` tag Rover world pos (-0.389627, 1.78579, -8.5) scale (4.2, 4.2) SpriteRenderer[bis_rocket_v1_cover, order 0, off]; BoxCollider2D[size 0.172x0.2829, trigger]; VisibleOnTrigger
  - `rockRepeller` tag Rover world pos (-0.389627, 1.78579, -0.5) scale (1, 1) BoxCollider2D[size 3.59812x7.536, trigger]
  - `Reverb` world pos (-0.389627, 1.78579, -0.5) scale (1, 1) AudioReverbZone
  - `InsideTrigger` world pos (-0.389627, 1.78579, -0.5) scale (1, 1) InsideRocketTrigger; BoxCollider2D[size 0.31x0.16, trigger]

### RockShatterPrefab

Instantiated at a relocated rock. Position is overridden. See 6.5.

- `RockShatterPrefab` world pos (-1000, -1000, 0) scale (1, 1) TimedObjectDestructor; AudioSource[Explosion1.wav, vol 0.963, pitch 1, play on awake, on]
  - `large flames` world pos (-1000, -1000, 0) scale (1, 1) EllipsoidParticleEmitter; ParticleAnimator; ParticleRenderer; Animation


## 5. Physics

### 5.1 World settings

| Setting | Value | Constant |
|---|---|---|
| Gravity | (0, -9.81), but every gravity scale is 0 | `PHYSICS_GRAVITY_*` |
| Fixed time step | 0.02 s, max step 0.3333 s | `FIXED_TIMESTEP`, `MAX_ALLOWED_TIMESTEP` |
| Velocity and position iterations | 8 and 3 | `PHYSICS_VELOCITY_ITERATIONS`, `PHYSICS_POSITION_ITERATIONS` |
| Velocity threshold | 1.0 | `PHYSICS_VELOCITY_THRESHOLD` |
| Max linear / angular correction | 0.2 / 8 | `PHYSICS_MAX_LINEAR_CORRECTION`, `PHYSICS_MAX_ANGULAR_CORRECTION` |
| Max translation / rotation speed | 100 / 360 | `PHYSICS_MAX_TRANSLATION_SPEED`, `PHYSICS_MAX_ROTATION_SPEED` |
| Baumgarte scale / TOI scale | 0.2 / 0.75 | `PHYSICS_BAUMGARTE_*` |
| Sleep: time, linear, angular | 0.5 s, 0.01, 2 | `PHYSICS_TIME_TO_SLEEP`, `..._SLEEP_TOLERANCE` |
| Queries hit triggers | true | |
| Physics materials | none anywhere | |

There is no PhysicsMaterial2D asset and every `m_Material` is null. So Unity's default material applies: friction 0.4, bounciness 0. Box2D's default friction is 0.6. The port has to set friction 0.4 and restitution 0 on every shape.

### 5.2 Layers

All objects are on layer 0 (Default). The layer collision matrix has every bit set (`extract/derived/layer_collision.json`), so every collider can touch every other collider. There are no layers or sorting layers beyond Default. One custom tag: `Rover` (`settings/TagManager.json`). The other tag in use is the built-in `Player`.

### 5.3 Rigid bodies

| Where | GameObject | Type | Mass | Linear drag | Angular drag | Fixed angle | Gravity scale | Sleep mode | Interpolation | Collision detection |
|---|---|---|---|---|---|---|---|---|---|---|
| scenes/Game | `Player` | dynamic | 1 | 0 | 0.05 | yes | 0 | 1 | 0 | 0 |
| scenes/Game | `Rover/RoverBody` | kinematic | 1 | 0 | 0.05 | no | 0 | 1 | 0 | 0 |
| scenes/Game | `Rover/bis_rover_v2_roof` | kinematic | 1 | 0 | 0.05 | no | 0 | 1 | 0 | 0 |
| prefabs/RockPrefab | `RockPrefab` | dynamic | 12 | 12 | 12 | no | 0 | 1 | 0 | 0 |
| prefabs/RocketPrefab | `RocketPrefab/RocketBody` | kinematic | 1 | 0 | 0.05 | no | 0 | 1 | 0 | 0 |

The Player is the only body that moves by script. Rocks move by the joint, by drag and by contact. Rover parts and the rocket are kinematic and never move. No script touches `Rigidbody2D.mass`, `drag` or `isKinematic`.

### 5.4 Colliders

Sizes are raw (before the scale of the GameObject) and in world units. Polygon pieces are in `values.h` (`PLAYER_BODY_PIECES_*`, `ROVER_ROOF_COLLIDER_PIECES_*`, `ROCKET_BODY_COLLIDER_PIECES_*`) and in `extract/derived/polygon_pieces.json`.

| Where | GameObject | Collider | Trigger | Body | Tag | Size or radius (raw) | World size | Offset (world) | GameObject world pos |
|---|---|---|---|---|---|---|---|---|---|
| scenes/Game | `Player` | CircleCollider2D | yes | `Player` | Player | r 0.2 | r 1 | (0, 0) | (6.56, 2.35) |
| scenes/Game | `Player` | PolygonCollider2D | no | `Player` | Player | 8 points | 3 pieces [3, 5, 4] points | (0, 0) | (6.56, 2.35) |
| scenes/Game | `Rover/RoverBody` | BoxCollider2D | yes | `Rover/RoverBody` | Rover | 0.25 x 0.267225 | 1.25 x 1.33613 | (-0.15, 0.331937) | (9.3, 4.5) |
| scenes/Game | `Rover/bis_rover_v2_roof` | PolygonCollider2D | no | `Rover/bis_rover_v2_roof` | Rover | 14 points | 7 pieces [3, 3, 3, 4, 4, 4, 5] points | (0, 0) | (9.3, 4.5) |
| scenes/Game | `Rover/rockRepeller` | CircleCollider2D | yes | static | Rover | r 3.29 | r 3.29 | (-0.15, -0.06) | (9.3, 4.5) |
| scenes/Game | `Rover/playerInsideRoverTrigger` | CircleCollider2D | yes | static | Untagged | r 0.12 | r 0.6 | (0, 0) | (8.92976, 4.51146) |
| prefabs/RockPlaceholderPrefab | `RockPlaceholderPrefab` | CircleCollider2D | yes | static | Untagged | r 0.045 | r 0.2025 | (0, 0) | (0.419964, 3.06142) |
| prefabs/RockPrefab | `RockPrefab` | CircleCollider2D | no | `RockPrefab` | Untagged | r 0.055 | r 0.2475 | (0, 0) | (-0.634035, -3.23763) |
| prefabs/RocketPrefab | `RocketPrefab/RocketBody` | PolygonCollider2D | no | `RocketPrefab/RocketBody` | Rover | 19 points | 7 pieces [3, 3, 3, 7, 5, 4, 6] points | (0, 0) | (-0.389627, 1.78579) |
| prefabs/RocketPrefab | `RocketPrefab/RocketCover` | BoxCollider2D | yes | static | Rover | 0.172 x 0.2829 | 0.7224 x 1.18818 | (-0.1638, -1.08024) | (-0.389627, 1.78579) |
| prefabs/RocketPrefab | `RocketPrefab/rockRepeller` | BoxCollider2D | yes | static | Rover | 3.59812 x 7.536 | 3.59812 x 7.536 | (-0.048791, -0.038908) | (-0.389627, 1.78579) |
| prefabs/RocketPrefab | `RocketPrefab/InsideTrigger` | BoxCollider2D | yes | static | Untagged | 0.31 x 0.16 | 0.31 x 0.16 | (0.01, -0.1) | (-0.389627, 1.78579) |

Polygon splitting: Box2D's linear slop is `0.005 * lengthUnitsPerMeter`. `lengthUnitsPerMeter` is the height of the player's body collider in world units: 1.141019 (`LENGTH_UNITS_PER_METER`, from `scenes/Game.json#Player:PolygonCollider2D`). The player sprite is 1.6 units high. With 1.6 the piece counts do not change. Slop is 0.005705095. The rule was checked by a port of `b2ComputeHull` (weld distance `4 * slop`, collinear distance `2 * slop`, 8 points at most): every piece keeps all its points.

| Collider | Source points | Pieces | Points per piece |
|---|---|---|---|
| Rover roof | 14 | 7 | 3, 3, 3, 4, 4, 4, 5 |
| Player body | 8 | 3 | 3, 5, 4 |
| Rocket body | 19 | 7 | 3, 3, 3, 7, 5, 4, 6 |

The plan expected 7, 2 and 7. Rover and rocket match. The player gets 3, not 2. The ear rule decides the cuts. I use "widest ear" as the ear triangle with the largest area. Other readings give different counts: cutting the ear with the smallest angle first gives 5, 2 and 6, and cutting the ear with the shortest diagonal first gives 5, 2 and 6. No reading I tried gives 7, 2 and 7 together. Largest-area ears leave the player polygon in 3 convex parts, and reaching 2 needs another cut order. 3 pieces is valid, because every piece passes the hull check.

### 5.5 Joint

The Rock prefab has a `DistanceJoint2D`: distance 0.55, anchor (0, 0), connected anchor (0, 0), not max-only, collide-connected false, break force Infinity, disabled at the start (`ROCK_JOINT_*`). `RockDragger.startPulling` sets `connectedBody` to the Player body and enables it. The joint is rigid (no spring settings exist on a Unity DistanceJoint2D).

### 5.6 Callbacks

Every collider is on layer 0, so every pair below is live. The rule in Unity 2D: a trigger event needs at least one body of the pair to be a Rigidbody2D (dynamic or kinematic); the callbacks go to both objects. Collision callbacks need two solid colliders. This is Unity's documented rule, not measured here.

| Pair | Event | Receivers and what they do |
|---|---|---|
| Rock (solid) enters Player trigger circle | trigger | `RockDragger.OnTriggerEnter2D` adds a candidate. Exit removes it. `Rock.OnTriggerEnter2D` ignores it (tag is Player) |
| Rock enters a Placeholder trigger | trigger | `RockPlaceholder.OnTriggerEnter2D` / `Exit2D`. `Rock` ignores it (tag is not Rover) |
| Rock enters `Rover/RoverBody` box, `Rover/rockRepeller` circle (r 3.29), `RocketBody` box repeller, `RocketCover` box | trigger | `Rock.OnTriggerEnter2D` and `OnTriggerStay2D`: tag `Rover` gives `relocate()` |
| Rock touches `Rover/bis_rover_v2_roof` or `RocketBody` (solid, tag Rover) | collision | `Rock.OnCollisionEnter2D` and `OnCollisionStay2D`: `relocate()` |
| Player body polygon enters `Rover/RoverBody` box | trigger | `Rover.OnTriggerEnter2D` shows the roof (player body only, `isTrigger == false`). Exit hides it |
| Player body polygon enters `RocketCover` box | trigger | `VisibleOnTrigger` shows the cover. Exit hides it |
| Player trigger circle or body polygon enters `Rover/playerInsideRoverTrigger` (r 0.6) | trigger | `RoverInsideTrigger` enter and exit |
| Player trigger circle or body polygon enters `RocketPrefab/InsideTrigger` | trigger | `InsideRocketTrigger.OnTriggerEnter2D`: win |
| Player body vs roof polygon, vs rocks, vs rocket body | collision | physical contact. No script handles `OnCollision*` on the player |
| Rock vs rock | collision | physical contact |

Because the roof polygon and the repeller circle are both tagged `Rover`, a rock that reaches the roof usually has crossed the repeller first. The repeller has radius 3.29 around the rover centre (offset (-0.15, -0.06)), while the roof is about 3.5 units wide. So rocks are removed before they touch the roof, unless they are dragged in.

Open points from the inventory, and what became of them (section 11 has the details):

1. Trigger-versus-trigger overlaps. The Player's trigger circle against `playerInsideRoverTrigger` (static trigger) and against placeholders is the only way the large circle can switch the rover screen on. The port reports them, as the plan says Unity does. The rover screen turns on at the same spot in both builds.
2. Whether Unity 5.4 sends `OnTriggerExit2D` when a rock inside a placeholder is destroyed. Still unverified. The port sends it.
3. The player's collider orientation after the half turn around Y (section 2.11). The port mirrors the body polygon. Not measured.
4. The spin of rocks and the player's speed with a rock on the joint. Measured in step 4, and the port matches.

## 6. Assets

### 6.1 Sprites and textures

All images are point filtered, except `superdamage.png` and `rm_tutorial_v4_2.png`. Their `.meta` filter mode is -1 (not set), and the build's Texture2D has filter mode 1 (bilinear) for both (`extract/build_comparison.json`, `BUILD_FILTER_MODE` in `tools/extract.py`). No mip maps, wrap clamp (also for those two: the build has wrap mode 1), 100 pixels per unit. The effective pivot is the sprite alignment, not the stored pivot: the three `marsTiles` slices store pivot (0, 0) but their alignment is Center, so the effective pivot is (0.5, 0.5). The build agrees (checked for all 56 sprites). `sprite.bounds.size.x` of a tile is 16 / 100 = 0.16. Sheet slice coordinates start at the bottom left, as in Unity. `asset_tables.h` also holds `y_from_top`.

The 3 tutorial images, `death.png`, `win.png`, `bis_screen_v2.png` are used as GUITexture only (no sprite).

| Image | px | ppu | filter | pivot (effective) | sheet slices | Used by |
|---|---|---|---|---|---|---|
| `Sprites/bis_exclamation_v2_2.png` | 32x32 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/bis_rock_dark_v3.png` | 16x16 | 100 | point | 0.5,0.5 | single | materials/Fire Smoke.mat.json, prefabs/RockPrefab.json |
| `Sprites/bis_rock_light_v3.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockPrefab.json |
| `Sprites/bis_rocket_v1.png` | 96x160 | 100 | point | 0.5,0.5 | single | prefabs/RocketPrefab.json |
| `Sprites/bis_rocket_v1_cover.png` | 96x160 | 100 | point | 0.5,0.5 | single | prefabs/RocketPrefab.json |
| `Sprites/bis_rover_v2.png` | 96x96 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/bis_rover_v2_roof.png` | 96x96 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/bis_screen_v2.png` | 220x220 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/bis_shadow_v1.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockShadowPrefab.json |
| `Sprites/rockDragTarget.png` | 24x24 | 100 | point | 0.5,0.5 | single | prefabs/RockDragTarget.json, scenes/Game.json |
| `Sprites/rockDragTarget_dash.png` | 24x24 | 100 | point | 0.5,0.5 | single | prefabs/RockDragTarget.json, scenes/Game.json |
| `Sprites/superdamage.png` | 254x360 | 100 | bilinear (build) | 0.5,0.5 | single | scenes/Intro.json |
| `Sprites/Cutscenes/bis_cutscene_v1_1.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/Cutscenes/bis_cutscene_v1_2.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/Cutscenes/bis_cutscene_v1_3.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/Cutscenes/death.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Death.json |
| `Sprites/Cutscenes/win.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Win.json |
| `Sprites/Slides/rm_tutorial_v4_1.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Story1.json |
| `Sprites/Slides/rm_tutorial_v4_2.png` | 660x440 | 100 | bilinear (build) | 0.5,0.5 | single | scenes/Story2.json |
| `Sprites/Slides/rm_tutorial_v4_3.png` | 660x440 | 100 | point | 0.5,0.5 | single | scenes/Game.json |
| `Sprites/ground/marsTiles.png` | 48x16 | 100 | point | 0.5,0.5 | marsTiles_0@0,0 16x16, marsTiles_1@16,0 16x16, marsTiles_2@32,0 16x16 | scenes/Game.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_00.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_01.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_02.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_03.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_04.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_05.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_06.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_07.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_08.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_09.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_10.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_paranoid_Animation 1_11.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_0.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_1.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_2.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_3.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_4.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_5.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_6.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_7.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_8.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/idle/bis_dude_idle_scratch_Animation 1_9.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/idle.anim.json |
| `Sprites/levelUI/bis_morse_correct_v1.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockPlaceholderPrefab.json |
| `Sprites/levelUI/bis_morse_dash_v1.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockPlaceholderPrefab.json |
| `Sprites/levelUI/bis_morse_dot_v1.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockPlaceholderPrefab.json |
| `Sprites/levelUI/bis_morse_wrong_v1.png` | 16x16 | 100 | point | 0.5,0.5 | single | prefabs/RockPlaceholderPrefab.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_0.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_1.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_2.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_3.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_4.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/pull/bis_dude_pulling_Animation 1_5.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/drag.anim.json |
| `Sprites/run/bis_dude_ani_Animation 1_0.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json |
| `Sprites/run/bis_dude_ani_Animation 1_1.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json |
| `Sprites/run/bis_dude_ani_Animation 1_2.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json |
| `Sprites/run/bis_dude_ani_Animation 1_3.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json, scenes/Game.json |
| `Sprites/run/bis_dude_ani_Animation 1_4.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json |
| `Sprites/run/bis_dude_ani_Animation 1_5.png` | 16x32 | 100 | point | 0.5,0.5 | single | animation/run.anim.json |

Animation frames (`Sprites/idle`, `run`, `pull`) are single 16 x 32 sprites, one file per frame.

### 6.2 Sound

16 sound files are used. The build is the reference for what the port plays. Eight MP3 files were re-encoded to Vorbis by the build (Unity 5 import), so their decoded length differs from the MP3 file. `seconds (build)` below is the length the build plays (frames / rate, `extract/build/audio.json`). The source length is in the note, and in `extract/assets/<path>.json` under `source` and `build`.

Layout in `port/assets` and why. Every sound is a 16 bit PCM WAV at its source path, with the extension changed to `.wav` when the source was an MP3. For 7 clips the file is the source WAV, because its PCM is byte for byte the build's PCM. For `Menu4.wav` (a 32 bit float WAV in the source) and for the 8 MP3 clips it is the build's own decoded audio, exported with UnityPy (FMOD). I chose WAV beside the source path because the port needs no MP3 or Vorbis decoder, the file names stay predictable, and the original MP3 files stay in the Unity project. `asset_tables.h` points at these files (`SOUNDS[].path`) and holds the build's channels, rate and frame count. Three MP3 files decode to exactly one MPEG frame (1152 samples, 0.0261 s) more than their Xing frame count says: `newMessage`, `Arcade_S`, `Victory_`. `static` plays at 11000 Hz in the build and not 11025 Hz, which is 0.23% slower than the file header says.

| File | container | ch | rate | bits | seconds (build) | importer 3D | stream | format | note |
|---|---|---|---|---|---|---|---|---|---|
| `8BitSounds/Explosion1.wav` | wav | 1 | 44100 | 16 | 0.5359 | True | 1 | -1 | exact, the source file is kept |
| `8BitSounds/Menu1.wav` | wav | 1 | 44100 | 16 | 0.0886 | True | 1 | -1 | exact, the source file is kept |
| `8BitSounds/Menu4.wav` | wav | 2 | 44100 | 32 (float) in the source, 16 in the build | 1.8462 | False | 1 | -1 | same frames as the source. The source is 32 bit float, the build 16 bit, every sample within 1. Port file is the build's: `8BitSounds/Menu4.wav` |
| `Assets/Free/Assets/8Bit/Explosion_04.wav` | wav | 1 | 48000 | 16 | 0.7306 | False | 1 | -1 | exact, the source file is kept |
| `Assets/Free/Assets/Footsteps/Footstep_Gravel_1.wav` | wav | 1 | 48000 | 16 | 0.5931 | True | 1 | -1 | exact, the source file is kept |
| `Assets/Free/Assets/Sci-Fi_Ambiences/Sci-fi_AmbienceLoop1.wav` | wav | 2 | 48000 | 16 | 115.4259 | True | 1 | -1 | exact, the source file is kept |
| `Assets/Free/Assets/UI2/GlassHitScroll6.wav` | wav | 2 | 48000 | 16 | 0.1632 | False | 1 | -1 | exact, the source file is kept |
| `Assets/Free/Assets/UI2/HitsAccept2.wav` | wav | 2 | 48000 | 16 | 0.5599 | False | 1 | -1 | exact, the source file is kept |
| `Sounds/Arcade_S-wwwbeat-8526_hifi.mp3` | mp3 (Vorbis in the build) | 1 | 44100 | 16 (decoded) | 0.6008 | False | 1 | 0 | source mp3 0.5747 s (Xing/Info frame count). Port file `Sounds/Arcade_S-wwwbeat-8526_hifi.wav` from the build, 26496 frames |
| `Sounds/FOLEY HORN PARTY TOY BLOW OUT 01.mp3` | mp3 (Vorbis in the build) | 2 | 44100 | 16 (decoded) | 2.351 | False | 1 | 0 | source mp3 2.351 s (size / bitrate estimate), same as the build. Port file `Sounds/FOLEY HORN PARTY TOY BLOW OUT 01.wav` from the build, 103680 frames |
| `Sounds/Victory_-arewhyae-8093_hifi.mp3` | mp3 (Vorbis in the build) | 2 | 44100 | 16 (decoded) | 18.4163 | False | 1 | 0 | source mp3 18.3902 s (Xing/Info frame count). Port file `Sounds/Victory_-arewhyae-8093_hifi.wav` from the build, 812160 frames |
| `Sounds/bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm.mp3` | mp3 (Vorbis in the build) | 2 | 32000 | 16 (decoded) | 3.528 | False | 1 | 0 | source mp3 3.5387 s (size / bitrate estimate). Port file `Sounds/bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm.wav` from the build, 112896 frames |
| `Sounds/drag.mp3` | mp3 (Vorbis in the build) | 2 | 44100 | 16 (decoded) | 0.7576 | True | 1 | 0 | source mp3 0.7576 s (Xing/Info frame count), same as the build. Port file `Sounds/drag.wav` from the build, 33408 frames |
| `Sounds/newMessage.mp3` | mp3 (Vorbis in the build) | 2 | 44100 | 16 (decoded) | 1.8024 | True | 1 | 0 | source mp3 1.7763 s (Xing/Info frame count). Port file `Sounds/newMessage.wav` from the build, 79488 frames |
| `Sounds/player_strain.mp3` | mp3 (Vorbis in the build) | 2 | 44100 | 16 (decoded) | 1.489 | True | 1 | 0 | source mp3 1.489 s (Xing/Info frame count), same as the build. Port file `Sounds/player_strain.wav` from the build, 65664 frames |
| `Sounds/static.mp3` | mp3 (Vorbis in the build) | 1 | 11000 (source 11025) | 16 (decoded) | 14.8375 | True | 1 | 0 | source mp3 14.8375 s (size / bitrate estimate), same as the build. Port file `Sounds/static.wav` from the build, 163213 frames |

The 18 AudioSources. "Spatial blend" and "reverb mix" are the values in the build. Distances are in world units. Rolloff: log is Unity's logarithmic curve (volume = min distance / distance, 1 inside the min distance), linear goes from 1 at min distance to 0 at max distance, custom uses the AudioSource's own curve (rock drag and shatter: from 1 at 0 to 0 at 1 of the range, `ROCK_DRAG_ROLLOFF_CURVE`, `SHATTER_SOUND_ROLLOFF_CURVE`). Doppler is 1 everywhere, priorities are listed. The scene pitch of the rover enter sound is -0.88. It never plays: the script overwrites the pitch before every `Play()` (`RoverInsideTrigger.cs:31` and `:52`), to 0.6 or 0.9.

| Where | Clip | vol | pitch | loop | on awake | enabled | min/max dist | rolloff | clip 3D flag | spatial blend (build) | reverb mix (build) | spread | priority |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scenes/Death `Victory_-arewhyae-8093_hifi` | `Sounds/Victory_-arewhyae-8093_hifi.mp3` | 0.35 | 1 | no | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Game `Menu4` | `8BitSounds/Menu4.wav` | 1 | 1 | no | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Game `Player` | `Sounds/player_strain.mp3` (silent: disabled, see 2.5) | 0.027 | 1 | no | no | off | 1/500 | log | yes | 1 | 1 | 0 | 128 |
| scenes/Game `Player/pivot/Sci-fi_AmbienceLoop1` | `Assets/Free/Assets/Sci-Fi_Ambiences/Sci-fi_AmbienceLoop1.wav` | 0.354 | 1 | yes | yes | on | 2.78327/500 | log | yes | 1 | 1 | 0 | 100 |
| scenes/Game `Player/footsteps` | `Assets/Free/Assets/Footsteps/Footstep_Gravel_1.wav` | 0.05 | 1 | yes | yes | off | 1/500 | log | yes | 1 | 1 | 0 | 128 |
| scenes/Game `Rover` | `Sounds/newMessage.mp3` | 0 | 0.95 | yes | yes | on | 0/25 | linear | yes | 1 | 1 | 1 | 128 |
| scenes/Game `Rover/playerInsideRoverTrigger` | `8BitSounds/Menu1.wav` | 0.143 | -0.88 | no | no | on | 1/500 | linear | yes | 1 | 1 | 0 | 107 |
| scenes/Game `Rover/playerInsideRoverTrigger/chatter` | `Sounds/static.mp3` | 0.288 | 1 | yes | yes | on | 0.42/4.6 | linear | yes | 1 | 1 | 0.447028 | 128 |
| scenes/Intro `Explosion_04` | `Assets/Free/Assets/8Bit/Explosion_04.wav` | 0.538 | 1.06 | no | yes | on | 1/500 | custom | no | 0 | 0 | 0.258333 | 192 |
| scenes/Story1 `Menu4` | `8BitSounds/Menu4.wav` | 1 | 1 | no | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Story2 `Menu4` | `8BitSounds/Menu4.wav` | 1 | 1 | no | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Title `clickSound` | `Assets/Free/Assets/UI2/HitsAccept2.wav` | 0.462 | 1 | no | no | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Title `highlightSound` | `Assets/Free/Assets/UI2/GlassHitScroll6.wav` | 0.177 | 1 | no | no | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Win `bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm` | `Sounds/bbc-030514-a-major-acoustic-guitar-rock-and-rock-140-bpm.mp3` | 1 | 1 | yes | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| scenes/Win `FOLEY HORN PARTY TOY BLOW OUT 01` | `Sounds/FOLEY HORN PARTY TOY BLOW OUT 01.mp3` | 1 | 1 | yes | yes | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| prefabs/RockPlaceholderPrefab `RockPlaceholderPrefab` | `Sounds/Arcade_S-wwwbeat-8526_hifi.mp3` | 1 | 1 | no | no | on | 1/500 | log | no | 0 | 0 | 0 | 128 |
| prefabs/RockPrefab `RockPrefab` | `Sounds/drag.mp3` | 0.158 | 0.45 | yes | yes | off | 0.904223/10 | custom | yes | 1 | 1 | 0.213889 | 186 |
| prefabs/RockShatterPrefab `RockShatterPrefab` | `8BitSounds/Explosion1.wav` | 0.963 | 1 | no | yes | on | 0.2/3 | custom | yes | 1 | 1 | 0.213889 | 128 |

The only AudioListener in the Game scene is `Player/pivot/audioListener`. Master volume 1, speed of sound 347, doppler factor 1, rolloff scale 1 (`AUDIO_*`). `AudioListener.volume` is a global and survives scene loads.

Reverb zones (3D sources have reverb mix 1): `Rover/Reverb` and `RocketPrefab/Reverb`, preset 22, min distance 1.3, max distance 2, room -1000, room HF -1000, decay time 2.81, decay HF ratio 0.14, reflections 429 at 0.014 s, reverb 1023 at 0.021 s, HF reference 5000, LF reference 250, diffusion 80, density 60 (`ROVER_REVERB_*`, `ROCKET_REVERB_*`). Each zone is tiny and sits at the vehicle. An AudioReverbZone acts on the listener position, not on each source. While the listener is inside the zone, every 3D source with reverb zone mix 1 is affected, wherever that source is. The listener is `Player/pivot/audioListener`, so the reverb is heard only while the player is within the zone (reverb at full strength inside min distance 1.3, fading out to max distance 2). The rover chatter, the new-message beep, rock drag and shatter sounds are affected during that time, and so are the player's own 3D sounds. The 2016 WebGL build renders no reverb at all. The 2016 Mac player holds the same preset table. The port plays the zones on every platform (`unity.c`, Reverb), on the whole mix rather than only the 3D sources (section 11).

### 6.3 Fonts

One font: `Joystix/joystix monospace.ttf` (57,596 bytes). The build embeds the same bytes (`extract/build/font.json`, `font_data_is_project_ttf`). The importer says font size 16, character spacing 1, padding 0. Used by every GUIText (sizes 11, 12, 13, 17, 30, 40, 50) and by the TextMesh letters (size 200, scale 0.1). The 29 fonts in `Computer Pack` are not used.

Metrics (dump: `extract/assets/Joystix/joystix monospace.ttf.json#ttf` and `extract/build/font.json`):

| Source | Value |
|---|---|
| TTF unitsPerEm | 1000 |
| TTF hhea ascent, descent, line gap | 1001, -199, 0 |
| TTF advance width | 828 for every glyph except one with 0 (390 glyphs, monospace) |
| TTF OS/2 typo ascender, descender, line gap | 801, -199, 200 (not used by Unity, see below) |
| TTF OS/2 win ascent, descent | 1001, 199 |
| Build Font object (`sharedassets1.assets#6`) | font size 16, ascent 16.016, descent -3.184, line spacing 19.2 |
| Build Font character spacing, padding, tracking | 0, 1, 1.0 |
| Build Font dynamic | yes: convert case -2, no character rects, no kerning values |

At size 16 the TTF gives ascent 1001 x 16 / 1000 = 16.016, descent -3.184, line height 1200 x 16 / 1000 = 19.2 and advance width 13.248. The build's numbers are the hhea values scaled to the font size. The OS/2 values do not appear. The build Font reports character spacing 0, so the importer's "character spacing 1" is not a spacing of one pixel. It shows up as `m_Tracking` 1.0, which is a multiplier (my reading, unverified). Because the font is dynamic, Unity rasterises glyphs from the TTF at the size of each GUIText and TextMesh. The port must do the same, with these metrics.

### 6.4 Animation

`Sprites/run/Player.controller` has one layer and 3 states with no transitions and no parameters. The default state is `run`, not `idle`. The code calls `Animator.Play(name)` every fixed update. The build agrees (frames, sample rates, loop flags, stop times).

| Clip | Frames | Sample rate | Length | Loop |
|---|---|---|---|---|
| `idle` | 22 (12 `idle_paranoid` then 10 `idle_scratch`) | 9 | 2.444 s | yes |
| `run` | 6 | 11 | 0.545 s | yes |
| `drag` | 6 | 11 | 0.545 s | yes |

Frame times are `i / sample rate`. The Animator runs in normal update mode with root motion on (no root curves exist). Tables: `asset_tables.h` (`CLIPS`, `ANIM_STATES`).

The shatter prefab has a legacy `Animation` component with a float curve (`animation/Stop Emitting ,5 sec.anim.json`): the emitter's `m_Emit` goes from 1 to 0 in 0.5 s (slope -2, 60 samples per second). It plays on awake, wrap mode once.

### 6.5 Particles

Only one live emitter: `RockShatterPrefab/large flames` (Unity legacy particle system). It is instantiated at the position of each relocated rock.

| Part | Value |
|---|---|
| Emitter | ellipsoid, not one-shot, emit starts true, 100 particles per second (min and max), energy 0.1 to 0.4 s, size 0.05 to 0.35, ellipsoid size 0, random velocity (5, 5, 5), emitter velocity scale 0, local, world and tangent velocity 0, random rotation on, simulate in world space |
| Animator | animate colour on, 5 colour keys (`SHATTER_ANIM_COLOR_0` to `_4`), damping 0.1, force (0, -1, 0), random force 0, size grow 0, autodestruct on |
| Renderer | length scale 0.1, max particle size 1, no stretch, UV tile 1 x 1, material `Fire Smoke` |
| Material | shader `Alpha-Diffuse` (`Legacy Shaders/Transparent/Diffuse`): alpha blend `SrcAlpha OneMinusSrcAlpha`, ZWrite off, queue Transparent (3000). Lit: the programs read the scene lights and the ambient light. It does not read the vertex colour, so the colour keys of the particle animator do not tint the particles (section 7.5, evidence in `extract/build/shaders.json`). `_Color` (1, 1, 1, 0.42), main texture `Sprites/bis_rock_dark_v3.png`. The source also names `smoketest2.psd` as `_AlphaTex`. The build's material has no such slot, so the file is not copied |
| Lifetime | the emitter stops after 0.5 s (animation). The root object is destroyed after 2.0 s (`TimedObjectDestructor`). The root also plays `Explosion1.wav` |

The Game scene also holds `Level/rockDestructionEffect`, inactive and never enabled by any script. Leave it out.

Step 8 measured how the legacy particle system runs these values. Section 11 has the rules.

## 7. Rendering

### 7.1 Camera and window

Orthographic, size 3.6 in Game (5 in other scenes). That is 7.2 world units over 440 pixels: 61.1 pixels per unit. Aspect = `Screen.width / Screen.height` = 1.5. The scripts read the live screen size (`CamerFocusSwitcher.cs:86`), so the port must pass the real window size. Colour space is Gamma: `extract/settings/ProjectSettings.json#m_ActiveColorSpace` is 0, and the build agrees (`extract/build/color_space.json`). Colours, textures and blending use the raw 0..1 values. No sRGB to linear conversion applies. Background is a solid colour (clear flags 1). Quality "Fastest": no vsync, no anti-aliasing, no shadows, no pixel lights (`QUALITY_*`).

### 7.2 Draw order

All SpriteRenderers have sorting order 0 and all use sorting layer Default. Unity then sorts by distance to the camera: far first. The camera looks along +z from z = -30, so a larger z is farther and drawn first. Game scene, far to near:

| z | Object |
|---|---|
| 0 | ground tiles |
| -0.5 | rover body, rocket body |
| -0.6 | rock shadows (set every frame, `Rock.cs:75`) |
| -1 | player |
| -3 | rocks |
| -4 | placeholders |
| -5 | drag target marker |
| -8.5 | rover roof, rocket cover (shown only near) |
| -12 | night overlay (a 60 x 60 plane child of the camera, material `nightMat`, shader Alpha-Diffuse: alpha blend `SrcAlpha OneMinusSrcAlpha`, ZWrite off, queue Transparent 3000, section 7.5. Colour (0, 0, 0, alpha), so the lighting term is black and only the alpha matters) |
| -20.5 | update indicator over the rover |
| -29 | decoded letters (TextMesh), cutscene images |

The night overlay is a MeshRenderer (built-in plane mesh, rotated, scale 6, local z +18 in front of the camera). Whether Unity sorts a mesh renderer with sprites by distance only in the transparent queue is unverified. The order above is consistent with the tutorial images covering everything.

Rocks are drawn over the player (z -3 is nearer than -1).

### 7.3 GUIText, GUITexture, TextMesh

GUIText and GUITexture are drawn after the camera, on top of the scene. Positions are viewport fractions of the 660 x 440 window. Text sizes are in points of the Joystix font; pixel correct is on; rich text is on; tab size 4; line spacing 1; pixel offset (0, 0).

| Object | Scene | Position | Size | Anchor | Align | Colour (rgba) | Starts |
|---|---|---|---|---|---|---|---|
| `solCounter` | Game | (0.5, 0.2) | 30 | middle centre | centre | 255, 255, 255, 49 | off |
| `screenGUIText` | Game | (0.07, 0.91) | 13 | upper left | left | 47, 208, 26, 255 | off |
| `visibiltyIndicator` | Game | (0.7, 0.99), z -25 | 11 | upper left | right | 255, 255, 255, 134 | off |
| `PLAY` | Title | (0.5, 0.2) | 50 | middle centre | centre | 255, 255, 255, 255 | on |
| `SD`, `WR`, `Title`, `LD` | Title | see 3.2 | 17, 12, 40, 12 | middle centre | centre | white | on |

The rover screen background GUITexture (`scenes/Game.json#RoverScreen:GUITexture`): texture `bis_screen_v2.png`, colour 0.5 (no tint), position (0.5, 0.5), transform scale (1, 1), pixel inset (x 0, y 0, width -220, height 0), off at the start, z -30. How Unity combines a non-zero scale with a negative inset width is unverified. Take a screenshot of the reference with the screen on before drawing it.

The GUITexture in Story1, Story2, Death and Win has scale 0 and inset (0, 0, 660, 440): the whole window.

GUI draw order is by the z of the element, higher in front (consistent with the screen text, z 0, over its background, z -30). Unverified.

TextMesh letters (`LetterPrefab`): font size 200, character size 1, object scale 0.1, anchor middle centre, align centre, colour (1, 1, 1, 0.5) with the alpha changed to 0.5 or 0.06 by the code, MeshRenderer off at the start.

### 7.4 Input

`settings/InputManager.json` axes in use: `Horizontal` (negative left, positive right, alt negative a, alt positive d) and `Vertical` (down, up, s, w). Both keyboard axes: sensitivity 3, gravity 3, dead 0.001, snap on, no invert (`INPUT_KEY_*`). The joystick axes of the same names: sensitivity 1, dead 0.19, vertical inverted, joystick 0 (`INPUT_STICK_*`). Direct key reads: Space (down, up), M (held), H, Escape, P, C and LeftShift (down), left mouse (down).

### 7.5 Shaders

Source: `extract/build/shaders.json` (parsed Shader objects and Materials of the 2016 build, with the first desktop OpenGL program of each shader as evidence). The build is the reference. The source scenes name the built-in shaders by Unity 4 ids (`resources/unity_builtin_extra#30` for `nightMat` and `Fire Smoke`, `library/unity default resources#10753` for `cutout`, `#10754` for the `Sprites-Default` material). The build resolves them as in the table. The ids differ from the ones in the source, so look them up in the build.

| Shader | Where in the build | Used by | Blend | ZWrite | Queue | Lighting | Vertex colour |
|---|---|---|---|---|---|---|---|
| `Sprites/Default` (asset name `Sprites-Default`) | `resources/unity_builtin_extra#10753` | every SpriteRenderer (`Sprites-Default` material, `globalgamemanagers.assets#2`) and `cutout.mat` (rocks, rock shadows) | `One OneMinusSrcAlpha`, premultiplied: the fragment multiplies rgb by alpha | off (Cull off) | Transparent 3000 | none | yes: output = texture x vertex colour x `_Color` |
| `Legacy Shaders/Transparent/Diffuse` (asset name `Alpha-Diffuse`) | `sharedassets4.assets#61` | `nightMat.mat` (night overlay), `Fire Smoke.mat` (shatter particles) | `SrcAlpha OneMinusSrcAlpha` (forward base pass), `SrcAlpha One` (forward add pass), colour mask RGB | off | Transparent 3000 | lit: scene lights (Lambert) and ambient light from spherical harmonics | no |
| `GUI/Text Shader` | `library/unity default resources#10101` | GUIText and TextMesh (the font material) | `SrcAlpha OneMinusSrcAlpha` | off (ZTest always, Cull off) | Transparent 3000 | none | yes: text colour |
| `Particles/Additive` (asset name `Particle Add`) | `sharedassets4.assets#63` | `Fairy Dust.mat`, used only by the inactive `Level/rockDestructionEffect` | `SrcAlpha One` | off | Transparent 3000 | none | yes |

What follows from the data:

- `cutout.mat` is not an alpha-test material. It uses `Sprites/Default`, which has no `_Cutoff` property. The `_Cutoff` 0.5 and `PixelSnap` 0 in the material do nothing. Pixel snap is off (no `PIXELSNAP_ON` keyword).
- The night overlay's material is `Alpha-Diffuse` with `_Color` (0, 0, 0, alpha) and no texture. The lighting term multiplies black, so the output is black with the alpha of `_Color` (the texture is the default white). It is drawn in the Transparent queue with ZWrite off.
- The shatter particles use the same shader. Its programs do not read the vertex colour (no `gl_Color` or `COLOR0` input in any of its programs). The legacy particle renderer writes the particle colour into the vertex colour, so the five colour keys of the particle animator probably have no visible effect here. The colour comes from `_Color` (1, 1, 1, 0.42) and the rock texture, multiplied by the light on the surface. The ambient light of the scene is (1, 0.671, 0.493), so the lit result is tinted. This is derived from the shader text, not measured (section 11).
- Evidence: `extract/build/shaders.json#shaders[*].programs` lists the attribute and uniform names found in the compiled programs, and `first_opengl_program` holds the first program as text.

## 8. Engine features the port must cover

| Feature | Where used |
|---|---|
| Frame loop: fixed updates at 50 Hz, then physics, trigger and collision callbacks, then Update, LateUpdate, deferred destroy | all scripts; `FixedUpdate` at `TopDownControls.cs:37`; `LateUpdate` at `CutsSceneScaleFixer.cs:16` |
| `Awake` before `Start` | `Title.cs:15`; `TimedObjectDestructor.js` |
| Time: `time`, `deltaTime`, `timeScale` (set to 0 and 1), time not reset by scene loads | `Earth.cs:70`, `:109`, `:190`; `Mars.cs:143`, `:150`, `:168`; `Intro.cs:17`; `NextSceneOnSpace.cs:13`, `:18`; `EndGame.cs:17`; `RockPlaceholder.cs:28`, `:56` |
| Update keeps running at timeScale 0, fixed updates and physics do not | `Earth.cs` pause, `RoverScreen.cs` |
| Input: `GetKey`, `GetKeyDown`, `GetKeyUp`, `GetMouseButtonDown`, `GetAxis` with smoothing | `RockDragger.cs:26`, `:36`; `CamerFocusSwitcher.cs:46`; `Earth.cs:73`, `:104`; `LevelLoader.cs:376`; `TopDownControls.cs:43`; `NextSceneOnSpace.cs:22`; `EndGame.cs:21` |
| `GetAxis` rules: sensitivity 3 rises, gravity 3 falls, snap on a sign change, dead zone 0.001, stick dead zone 0.19 | `InputManager.asset` |
| Mouse over GUIText: enter, exit, up | `GUITextButton.cs:15` to `:30` |
| Transform: position, rotation about Y, local scale, parent, world transform | most scripts |
| Prefab `Instantiate` with position and rotation, `Destroy`, deferred to frame end, a destroyed object compares equal to null | `Rock.cs:34`, `:130`; `RockGenerator.cs:40`, `:66`; `LevelLoader.cs:91`, `:222`, `:297`, `:315`; `Mars.cs:112`; `Earth.cs:219`; `RockDragger.cs:57` |
| `GetComponent` by type and by string, `GetComponentsInChildren` | `LevelLoader.cs:224`, `:338`, `:345`; `Rock.cs:24`, `:33`; others |
| `SceneManager.LoadScene` by name and index | `Intro.cs:23`, `Title.cs:27`, `Rocket.cs:21`, `RoverScreen.cs:113`, `NextSceneOnSpace.cs:29`, `EndGame.cs:28` |
| `Application.OpenURL` | `Title.cs:22` |
| `Random.Range` int and float | `Mars.cs:120`, `RockGenerator.cs:29`, `:55`, `:56` |
| `Mathf`: Round (half to even), Ceil, Abs, Clamp, Lerp, Max, Infinity, SmoothDamp | `Mars.cs`, `RoverScreen.cs:96`, `CamerFocusSwitcher.cs`, `TopDownControls.cs`, `iTween.cs:5256` |
| SpriteRenderer: sprite, enabled, colour, sprite size and pivot | many |
| Camera: orthographic size get and set, `Camera.main`, `Screen.width`, `Screen.height` | `CamerFocusSwitcher.cs`, `CutsSceneScaleFixer.cs:11` |
| GUIText: text, colour, enabled, anchor, alignment, font size, viewport position. GUITexture: enabled, inset. TextMesh: text, colour, anchor | `Mars.cs:158`, `RoverScreen.cs:107`, `GUITextButton.cs:24`, `LevelLoader.cs:319` |
| MeshRenderer with a transparent coloured plane, `material.color` alpha | `CamerFocusSwitcher.cs:62` to `:64` |
| AudioSource: `Play`, `Stop`, `enabled` (off to on starts a play-on-awake source), volume, pitch (also negative), loop, 3D position, rolloff (log, linear, custom), spread, priority | `RockDragger.cs:79`, `RoverInsideTrigger.cs:30` to `:33`, `Rock.cs:60`, `TopDownControls.cs:49` to `:76`, `Earth.cs:114`, `RockPlaceholder.cs:65`, `GUITextButton.cs:16` |
| `AudioListener.volume` global, one listener with a rotation | `Earth.cs:60`, `TopDownControls.cs:69` |
| AudioReverbZone | `Rover/Reverb`, `RocketPrefab/Reverb` |
| Animator `Play(string)` on a three state controller of sprite clips | `TopDownControls.cs:72`, `:78` |
| Legacy `Animation` with a float curve on `m_Emit`, `Invoke`, `DestroyObject` | `RockShatterPrefab`, `TimedObjectDestructor.js` |
| Legacy particles: ellipsoid emitter, particle animator (colour keys, force, damping, autodestruct), particle renderer, alpha-diffuse material | `RockShatterPrefab/large flames` |
| Physics2D: dynamic, kinematic and static bodies; circle, box, polygon colliders with triggers; linear and angular drag; fixed angle; mass set directly; velocity get and set; `AddTorque`; DistanceJoint2D with `connectedBody` and `enabled`; tags on colliders; `Collider2D.isTrigger`; `OnTrigger*2D` enter, stay, exit; `OnCollision*2D` enter, stay | see section 5 |
| Events and delegates | `Title.cs`, `GUITextButton.cs` |
| `List<T>`, `string.Split`, string concatenation, `char` loops | `LevelLoader.cs`, `Morse.cs`, `RoverScreen.cs` |
| JSON parser | replaced by tables |

## 9. Differences from the plan

1. **A 25th script.** The plan counts 24. The shatter prefab also carries `TimedObjectDestructor.js` (UnityScript, Standard Assets, `timeOut` 2.0). The port needs `Invoke` and `DestroyObject` for it, or a plain 2 s timer.
2. **The shatter effect is not one-shot.** The plan says "one-shot emission". The emitter has `m_OneShot` false and emits 100 particles per second. A legacy `Animation` component turns emission off after 0.5 s. The prefab also plays an explosion sound, and its material is Alpha-Diffuse with the rock sprite as texture (section 6.5). The engine layer needs a float-curve animation on `m_Emit`.
3. **Audio is not "mostly 3D".** The plan says most sounds are imported as 3D. 8 of 16 clips have the 3D flag. The scene files say panLevel 1.0 for every source, but the build makes the 10 sources with non-3D clips fully 2D and the 8 others fully 3D with reverb mix 1. `values.h` follows the build. So the Title, Story, Death, Win, Intro, Game start sound and placeholder sounds are plain 2D. 3D panning matters for the rover chatter, the new-message beep, the rock drag and the shatter. Footsteps, strain, ambience and the enter sound sit on the player, which is the listener, so they have no distance effect (section 6.2).
4. **Reverb is not skippable by default.** The zones are tiny (max distance 2 around the rover and rocket) but they apply to all 3D sources there, reverb mix 1. The plan leaves it to a listening test; the data supports that test. The WebGL build has the same settings. Step 7 settled it: the WebGL player creates no effect node, so it plays no reverb. The Mac build could not be heard headless. The port now plays the reverb from the zone's I3DL2 values, as the Mac build would.
5. **The rover is protected by a trigger repeller.** The plan describes the rover's trigger and roof. It does not list `Rover/rockRepeller`, a circle trigger of radius 3.29 with the tag `Rover`. Rocks relocate when they enter it, long before they touch the roof. The rocket has the same: a 3.6 x 7.5 box repeller and a cover trigger, both tagged `Rover`.
6. **Friction.** No physics material exists, so Unity's default friction 0.4 applies. Box2D defaults to 0.6.
7. **Polygon pieces: 7, 3, 7.** The plan expected 7, 2, 7. Section 5.4 explains why.
8. **Morse table size.** The plan says 40 letters. The file has 54 entries: 10 digits, 26 letters and 18 symbols. The escape quirk of `DOUBLE_QUOTE` stays (section 2.20). `level1.json` is an early draft and is not used.
9. **Scene values override script defaults.** `Earth.telemetryDelay` is 1.8 (default 3.5), `maxMessageSoundVolume` 0.3 (0.4), `LevelLoader.gap` (1, 3) ((3, 3)), `RoverScreen.startingLifeSupport` 14 (10), `RockGenerator.numRocks` 100 (40; the plan has this one). The first rover message is a string stored in the scene, not in `levels.json`.
10. **Time scale of a sol.** `Mars.timeScale` is 2000, so a sol lasts 44.37 s of real time. The player dies after 15 sols (665.5 s) of unfrozen time. The first sol begins as soon as the tutorial is dismissed. Earth is visible only from 25% to 75% of each sol (11.1 s to 33.3 s into it). `Earth.Update` decodes only when 1.8 s have passed since the sol began and Earth is visible.
11. **The camera starts far away and does not move while paused.** The camera is saved at (-1000, -1000, -30), and the game starts paused (the tutorial sets `timeScale` 0). `iTween.MoveUpdate` is `Mathf.SmoothDamp` with a fresh zero velocity, `smoothTime` of 0.01 s (following the player) or 0.05 s (rover, overview) and `Time.deltaTime`. At `deltaTime` 0 it does not move. So the tutorial shows over an empty background, and the camera snaps to the player in a few frames after the player presses Space. The plan only says "port that one function"; SmoothDamp is the function. The camera size easing (1/10 per frame) and the overlay easing (1/5 per frame) do run while paused.
12. **Animator default state is `run`.** The plan lists three states and no transitions. The default state is `run`. All clips loop.
13. **Sprite pivots.** Stored pivots in the `.meta` files are wrong for the tile sheet (stored (0, 0), effective centre). Use the alignment. `asset_tables.h` holds the effective pivot, checked against the build.
14. **A stray script.** `Rocket` is attached to `LetterPrefab`. Harmless.
15. **Layer matrix.** Everything is on layer 0 and everything collides. The plan does not say this. No layer logic is needed.
16. **The UI sprite `RockDragTarget` is not a prefab in the build.** Only the scene instance matters. The prefab's colour alpha (0.627) differs from the scene's (0.353). The scene value is used.
17. **GUITexture sizes.** Four full-screen images use scale 0 and pixel inset 660 x 440. The rover screen uses scale 1 with inset width -220. The plan says only "GUITexture for the rover screen background".
18. **Hard-coded values that are not in the scene.** These live only in code and must be copied with a comment: `SOL_IN_HOURS` 24.648, solCounter duration 3 s, decoded message duration 2 s, Rock `sfxMinVelocity` 0.2 and torque factor 13, drag shadow offset (0, -0.3, -0.6), footstep threshold 0.5 and pitches 0.9 and 1.9, walk speed 7.2 with factor 0.8, player clamp margins 0.5 and 0.8, rock spawn range 0.5, camera ease factors 0.1 and 0.2 and speeds 0.2 and 1.0, night factor 0.85 and ease 0.2, the cheat key, the 31 character line width, 12 line capacity, `yStart` offset -1.394218, indicator fade 1.2, placeholder clear time 0.6, intro 2.0 and 0.1, end screens 5 s. Every one of them, and every other string and number that lives only in the code, is in `port/src/code_constants.h` with its file and line. `tools/extract.py` checks each against the source.
19. **Update order (plan 6.3, item 10).** Confirmed: no execution order is set anywhere. Also `RoverScreen.Start` calls `printScreen` once, which reads `mars.hoursUntilNextSol` before `Mars.Update` ran. The first text has zeros in those fields. It is overwritten in the first `Update`.
20. **Build facts the plan said were unconfirmed.** The Mac build is Unity 5.4.3f1, x86_64. The WebGL build is IL2CPP-style (no managed DLL) and holds identical gameplay data. Both builds' project settings: 660 x 440, not resizable, run in background off, Unity splash on.

## 10. Not extracted, and open questions

- Unity engine behaviour that the data cannot show is listed in section 11, with what steps 2 to 8 measured. Most of it was measured on the 2016 WebGL build, because the Mac build cannot be heard or captured frame by frame headless. The shader programs are not engine behaviour. They are in `extract/build/shaders.json` (section 7.5).
- IL logic of `Assembly-CSharp.dll`: done. See `extract/decompile_check.md` (all 24 scripts MATCH).
- `smoketest2.psd` is not copied. It is only named by a material slot that the shader does not have. `Standard Assets` prefabs other than the ones listed are not dumped.
- The `.meta` file of the font has no per-size data. Unity renders GUIText from the TTF at the size given by the GUIText.
- Unused assets and why: `extract/derived/used_assets.json` (`excluded`, 152 files).

## 11. Engine behaviour, measured

The data cannot show these. Steps 2 to 8 of the port measured them on the 2016 WebGL build, unless a line says otherwise. Each line gives the result, then the source lines that depend on it. The C file named holds the full comment.

- Trigger against trigger overlaps (the Player trigger circle against `playerInsideRoverTrigger` and placeholders). Box2D 3 sensors detect sensors, so the port reports them. Events count per collider pair and go to both owners. The rover screen turns on at the same spot in both builds. `RoverInsideTrigger.cs:26`, `RockPlaceholder.cs:52`, `RockDragger.cs:93`. `physics.c`.
- The exit event when a rock is destroyed inside a placeholder. Not measured. The port sends exit events at destroy time, while the rock still exists, and ignores Box2D's later end events. Unity 5.6 added `callbacksOnDisable`, on by default, which suggests that 5.4 sent them. `RockPlaceholder.cs:98`, `Rock.cs:130`, `RockGenerator.cs:66`. `physics.c`.
- The raw quaternion Y flip. Unity turns it into a half turn around Y, 179.4 degrees. Walking right sets the listener's forward to about (0.011, 0, -1). Walking straight up or down faces left. The port mirrors the body polygon while facing left, without a measurement. `TopDownControls.cs:63` to `:69`. `player.c`.
- Rock spin, rock drag and the player's speed with a rock on the joint. Measured in step 4, and the port matches. Unity's drag is Box2D 2.x damping, v *= 1 / (1 + h * drag) per 0.02 s step. The port spreads that over Box2D 3's sub-steps (`physics_damping`). Torque goes to Box2D unchanged. `Rock.cs:67`, `RockDragger.cs:71` to `:80`. `physics.c`, `rock.c`.
- `GetAxis` smoothing. Unity's documented rule matches: sensitivity 3 up, gravity 3 down, snap on a sign change. Axes move with scaled time, so a pause freezes them. An arrow held through the paused tutorial ramps up over 20 frames after Space. `TopDownControls.cs:43`, `:44`, `Earth.cs:190`. `unity.c`.
- 3D audio. Volume is AudioSource volume times the rolloff, with the 3D distance, so z counts. Custom curves are read at distance / max distance. Pan follows the listener, flip included, with HRTF in Chrome. The port uses WebAudio's equal-power law for the same angle. Doppler moves the pitch with the frame's velocity, and it is back to 1 on frames with no physics step. Spread has no WebGL counterpart. The WebGL build creates no ConvolverNode or other effect node, so the reverb zones do nothing there. The port plays them anyway, from the zone values, as the Mac build would. How FMOD in the Mac build pans, spreads and applies Doppler was not measured. `RoverInsideTrigger.cs:31` and `:52` (the -0.88 pitch never plays: the enter sound starts at playbackRate 0.6). `unity.c`.
- GUIText, TextMesh and GUITexture layout. Glyphs are unhinted and antialiased, drawn at whole pixel origins. The advance is 828 font units at the text's size, rounded to whole pixels. Lines sit a whole number of pixels apart, rounded: 16 px for the rover screen's 13 pt text. The rover screen GUITexture, scale 1 with inset width -220, covers the left 440 px. Title button hit boxes match. `Mars.cs:158`, `RoverScreen.cs:107`, `GUITextButton.cs:15` to `:30`, `LevelLoader.cs:319` to `:324`. `unity.c`.
- `Play()` on a disabled AudioSource plays nothing. The build logs "Can not play a disabled audio source" at every pull, so the strain sound is silent. Turning the flag on starts a play-on-awake source from the beginning, and turning it off stops it. The footsteps and the rock drag sound work this way. `RockDragger.cs:79`, `Rock.cs:60`, `:70`, `TopDownControls.cs:61`, `:76`. `unity.c`.
- Legacy particles, the rock shatter. Each frame emits the whole part of 100 * deltaTime and drops the rest. The emitter runs while the `m_Emit` curve is above 0, so new particles appear until 0.5 s, and the last one fades out 0.80 to 0.83 s after the shatter. Random velocity is up to 5 units/s each way. Damping applies per second: v *= damping ^ deltaTime. A particle is the dark rock texture times the ambient light, at alpha 0.42. The colour keys do nothing, because the shader ignores vertex colour. Particles draw over rocks at the same z. At timeScale 0 the effect waits. `RockGenerator.cs:64`. `effects.c`.
- Sleeping rocks against new triggers. Not measured separately. The port relies on Box2D 3 sensor events. The step 5 playthroughs placed and checked answers the same way on both builds. `Rock.cs:99` to `:108`, `RockPlaceholder.cs:52`.
- `Animator.Play` every fixed update. The port follows Unity's documented rule: it does nothing while that state already plays. The pose starts in the default state, `run`. Not measured separately. `TopDownControls.cs:72`, `:78`. `unity.c`.
- A scene change stops every sound. The title's click sound stops 19 ms after the click, when Story1 loads. `Time.time` inside physics callbacks is the fixed time, at most one fixed step behind the frame. `RoverScreen.cs:113`, `Rocket.cs:21`, `EndGame.cs:28`, `RockPlaceholder.cs:56`. `unity.c`, `rock_placeholder.c`.
- Equal-z draw ties. Particles draw over rocks. The order of rocks among themselves is the port's slot order. The 2016 order was not measured. `Mars.cs:112`, `RockGenerator.cs:58`. `game.c`.
- The first frame. The port's Update runs at a fixed 60 Hz, so the first `deltaTime` is 1/60 s. The 2016 build runs one physics step in the Game scene's first frame, before the tutorial pauses it. The port may not, so a shatter from that step can sound after Space instead. `Mars.cs:143`, `:150`, `Earth.cs:117`, `Intro.cs:17`. `unity.c`.
