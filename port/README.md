# Rockmorse C port: notes

Working notes for the port. Step 10 of the plan turns them into the final docs.

## Notes for step 10

### Reverb: left out

The Game scene has two AudioReverbZones, `Rover/Reverb` and `RocketPrefab/Reverb`. The port does not render them, and verblib is not added. The reasons:

- The 2016 WebGL build has no reverb. Its audio code (`RockMorse.jsgz`, the `_JS_Sound_*` functions) never creates a ConvolverNode or any other effect node. A hook on every WebAudio constructor saw only GainNode, PannerNode and AudioBufferSourceNode while walking into the rover.
- The 2016 Mac build is the plan's reference for sound, but it cannot be heard in the headless setup used for steps 7 and 8. So the reverb could not be confirmed by ear.
- The zones are small: full strength within 1.3 units of the vehicle, nothing past 2. The player is that close only while inside the rover or the rocket.

If someone listens to the Mac build and hears the reverb, verblib on raylib's audio stream is the plan's option (plan section 4).

### What the 3D audio follows

The WebGL build, measured with hooks on its WebAudio calls (`src/unity.c`, audio section):

- Volume is AudioSource.volume times the rolloff. Distances are 3D, so z counts. Logarithmic is min / distance, flat past the max distance. Linear runs from 1 at the min distance to 0 at the max. Custom curves are read at distance / maxDistance. Every logged value matched to 4 or 5 digits.
- Pan uses the listener's orientation, including the kept flip (the listener faces the other way to the player). WebGL pans with Chrome's HRTF model. raylib cannot do that, so the port uses WebAudio's equal-power law for the same azimuth.
- Doppler is on in the build. The pitch follows the frame-to-frame velocity of the listener and the source, up to about 3% while walking. It drops back to exactly 1 on frames with no physics step.
- Spread has no WebGL counterpart, so the port leaves it out. FMOD in the Mac build may render it.

Not verified: how FMOD in the Mac build pans, spreads and applies Doppler. The measured speed of sound was about 340 (noisy); the port uses `AUDIO_SPEED_OF_SOUND` (347) from the project settings.

### Rows `values.h` still needs

The port uses these dump values through `#ifndef` fallbacks until `tools/extract.py` adds the rows:

- `ROVER_CHATTER_POS_X/Y/Z`: `scenes/Game.json#Rover/playerInsideRoverTrigger/chatter:@transform:world_position` (`src/rover.c`).
- `GAME_AMBIENT_LIGHT_R/G/B`: `scenes/Game.json#scene_settings/RenderSettings:m_AmbientLight` (`src/effects.c`).

### Known small differences

- The 2016 build runs one physics step in the Game scene's first frame, before the tutorial pauses it. Rocks that start in the rover's repeller shatter there, and their sound plays under the tutorial. The port's first frame may or may not run a step, so these sounds can come at the first step after Space instead. The effects look the same, because they wait during the pause in both.
- The overlap order of rocks at the same z is the port's slot order. The 2016 build's order was not measured.
