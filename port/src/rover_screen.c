// RoverScreen.cs, on the Game scene's RoverScreen object: the screen inside
// the rover. Only its switch is here yet. The text, life support and death
// come in step 6 of the plan.
#include "game.h"

RoverScreen roverScreen;

// original: //private bool powerOn;
// original: // Use this for initialization
void rover_screen_start(void)
{
    roverScreen = (RoverScreen){
        // background = GetComponent<GUITexture>(). Unlike the full screen
        // images, it has scale 1 (ROVER_SCREEN_SCALE_*).
        .background = {
            .image = ROVER_SCREEN_BACKGROUND_TEXTURE,
            .position = {ROVER_SCREEN_BACKGROUND_VIEWPORT_POS_X, ROVER_SCREEN_BACKGROUND_VIEWPORT_POS_Y,
                         ROVER_SCREEN_BACKGROUND_VIEWPORT_POS_Z},
            .scale = {ROVER_SCREEN_SCALE_X, ROVER_SCREEN_SCALE_Y, ROVER_SCREEN_SCALE_Z},
            .pixelInset = {ROVER_SCREEN_BACKGROUND_PIXEL_INSET_X, ROVER_SCREEN_BACKGROUND_PIXEL_INSET_Y,
                           ROVER_SCREEN_BACKGROUND_PIXEL_INSET_WIDTH, ROVER_SCREEN_BACKGROUND_PIXEL_INSET_HEIGHT},
            .color = {ROVER_SCREEN_BACKGROUND_COLOR_R, ROVER_SCREEN_BACKGROUND_COLOR_G,
                      ROVER_SCREEN_BACKGROUND_COLOR_B, ROVER_SCREEN_BACKGROUND_COLOR_A},
            .enabled = ROVER_SCREEN_BACKGROUND_ENABLED,
        },
        .screenGUIText = GUI_TEXT(ROVER_SCREEN_TEXT),
        .startingLifeSupport = ROVER_SCREEN_STARTING_LIFE_SUPPORT,
    };

    roverScreen.startingLifeSupport += ROVER_SCREEN_LIFE_SUPPORT_BONUS; // original: // for past day (spends immediately)

    roverScreen.lifeSupport = roverScreen.startingLifeSupport;

    // printScreen() comes in step 6.

}

void rover_screen_set_on(bool _on)
{

    if (roverScreen.on == _on) return;

    roverScreen.on = _on;
    roverScreen.background.enabled = roverScreen.on;
    roverScreen.screenGUIText.enabled = roverScreen.on;

    if (roverScreen.on) {
        earth_player_is_in_rover();
    }
}
