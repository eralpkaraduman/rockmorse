# Rockmorse

A faithful C port of Rockmorse, a 2D game Super Damage made for Ludum Dare 30 in August 2014. The theme was "Connected Worlds".

You are an astronaut stranded on Mars. Earth sends you a message each sol, and you answer by dragging rocks into Morse code. Dark rocks are dashes and light rocks are dots. Earth can only read your answer in daylight. Answer every message before life support runs out, and a lander comes to take you home.

Super Damage was Eren Ozel (https://erenozel.art) and Eralp Karaduman (https://eralp.dev).

The original was made in Unity. Its source and the 2016 reference builds are at https://github.com/superdamage/LD30. This port follows the post-compo version at its last commit, `19ad7cb`, from December 2016. Post-compo work starts at commit `9419fcc`. In October 2026 the game was rebuilt in C with raylib and Box2D. It needs nothing from Unity to build or run.

## Play it

- [Game page](https://eralpkaraduman.github.io/rockmorse/), with the description, controls and screenshots.
- [Game only](https://eralpkaraduman.github.io/rockmorse/play/), full window.

It runs in the browser on desktop with a keyboard, mouse or gamepad, and on phones and tablets with on-screen controls.

To play offline on a Mac with Apple silicon, unzip `dist/rockmorse-macos-arm64.zip` and run `Rockmorse/rockmorse`. To build it for macOS, Windows, Linux or the web, see `port/README.md`.

## Controls

| Action | Keys | Gamepad | Touch |
|---|---|---|---|
| Walk | Arrows or WASD | D-pad or left stick | D-pad |
| Pull a rock | Hold Space | Hold A | Hold Pull |
| See the whole answer | Hold M or Tab | Hold the left shoulder | Hold Map |
| Close a message, go on | Space or Enter | A | Pull, or a tap |
| Help on or off | H, Shift, Esc or P | Start | Help |
| Title: Play | Click, Space or Enter | A | Tap |

The cheat is "hold Ctrl, press C". It fills the current answer's slots with the right rocks.

## Builds in dist/

- `rockmorse-web-itch.zip`: the web version, for itch.io. `port/ITCH.txt` has the page text and settings.
- `rockmorse-macos-arm64.zip`: for Macs with Apple silicon. The build is unsigned, so the first time, right-click `rockmorse` and choose Open.

## What is here

- `extract/`: the original game data as readable JSON. It holds every scene, prefab, animation clip, material and project setting. It also holds facts read from the 2016 builds, and every original code comment.
- `tools/extract.py`: reads the Unity project and the 2016 Mac build. It writes `extract/`, the C headers in `port/src/` and the used assets in `port/assets/`. `tools/build_extract.py` reads the build for it.
- `tools/check_comments.py`: checks that every original code comment is kept in the C code.
- `tools/compare_build.py`, `tools/decompile_check.py`: compare the Unity project with the 2016 build and its decompiled scripts.
- `INVENTORY.md`: everything the port had to implement, with sources, and what the port measured on the 2016 builds.
- `port/`: the C port. See `port/README.md`.
- `port/ITCH.txt`, `port/rockmorse-cover-630x500.png`, `port/screenshots/`: the itch.io page text and images.
- `dist/`: ready-made builds of the port.

The 2016 reference builds stay in the LD30 repo under `Unity/Bin/`. This repo does not copy them.

## Deliberate changes

The port plays, looks and sounds like the 2016 build. Original bugs stay in, each with a comment. A few things changed on purpose.

- Pressing H, Escape or P during the 2 s overview after a decode froze the game. The port ignores those keys while that overview shows.
- The original cheat was Shift and C pressed in the same frame, which almost never worked. Shift now toggles the help, so the cheat is "hold Ctrl, press C".
- Enter, Tab and Shift work next to the original keys, and so do a gamepad and touch controls. Space and Enter start the game from the title.
- The Unity splash screen is left out, as it is not part of the game.
- The rover and the lander have reverb on the web too. The 2016 WebGL build never played it, but the Mac build did (`port/README.md`).
- The web version ships its sounds as QOA files, so the download is 6 MB instead of 28 MB.

## Credits

Rockmorse by Super Damage: Eren Ozel and Eralp Karaduman, 2014.

The C port was written in 2026 with Claude Code, Anthropic's coding agent, running the Claude Opus 5.5 model.
