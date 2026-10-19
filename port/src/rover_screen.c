// RoverScreen.cs, on the Game scene's RoverScreen object: the screen inside
// the rover. It shows Earth's message, wrapped at 31 characters, then life
// support, the time to the next sol and whether the satellite sees you. It
// counts life support down by the sol, and loads Death when it runs out.
#include <stdio.h>
#include "game.h"

RoverScreen roverScreen;

// RoverScreen.cs private fields.
static float maxCharsPerLine;
static int numLineCapacity;

// original: //public string question = "";

// The screen text is built here, as C# builds its string.
static char linedText[GUI_TEXT_MAX];

static void append(const char *s)
{
    size_t n = strlen(linedText);
    snprintf(linedText + n, sizeof linedText - n, "%s", s);
}

// original: //private bool powerOn;
// original: // Use this for initialization
void rover_screen_start(void)
{
    maxCharsPerLine = ROVER_SCREEN_MAX_CHARS_PER_LINE;
    numLineCapacity = ROVER_SCREEN_LINE_CAPACITY;
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
        .visibilityIndicator = GUI_TEXT(VISIBILITY_INDICATOR),
        .startingLifeSupport = ROVER_SCREEN_STARTING_LIFE_SUPPORT,
    };

    roverScreen.startingLifeSupport += ROVER_SCREEN_LIFE_SUPPORT_BONUS; // original: // for past day (spends immediately)

    roverScreen.lifeSupport = roverScreen.startingLifeSupport;

    rover_screen_print_screen();

}

// Pads a number to two digits, as "0" + s when s is one character long.
static const char *two_digits(int value)
{
    const char *s = TextFormat("%d", value);
    if (strlen(s) < ROVER_SCREEN_PAD_WIDTH) s = TextFormat("%s%s", ROVER_SCREEN_PAD, s);
    return s;
}

const char *rover_screen_print_screen(void)
{

    char text[GUI_TEXT_MAX];
    snprintf(text, sizeof text, "%s%s%s", ROVER_SCREEN_HEADER, ROVER_SCREEN_HEADER_RULE, levelLoader.currentQuestion);

    linedText[0] = 0;
    int numChars = (int)strlen(text);
    int charIndex = 0;
    int charsInLine = 0;
    int lineCount = ROVER_SCREEN_HEADER_LINES;

    // Kept bug (plan 6.3, item 8): a new line goes in after every 31
    // characters, and the newlines already in the text count as ordinary
    // characters. So the header's two lines shorten the first wrapped line.
    while (numChars > 0) {

        if (charsInLine >= maxCharsPerLine) {
            append(ROVER_SCREEN_NEWLINE);
            charsInLine = 0;
            lineCount++;
        }
        const char c[2] = {text[charIndex], 0};
        append(c);

        charIndex++;
        charsInLine++;
        numChars--;
    }

    int remainingLines = numLineCapacity - lineCount;

    while (remainingLines > 0) {
        append(ROVER_SCREEN_NEWLINE);
        remainingLines--;
    }

    // original: // print print seperator
    append(ROVER_SCREEN_SEPARATOR);

    append(ROVER_SCREEN_NEWLINE); // original: // gap

    // original: // print life support
    append(ROVER_SCREEN_LIFE_SUPPORT_LABEL);
    append(two_digits(roverScreen.lifeSupport));
    append(ROVER_SCREEN_LIFE_SUPPORT_UNIT);

    // original: // print life support
    // (int) casts truncate toward zero, as in C#.
    char d1[16], d2[16];
    snprintf(d1, sizeof d1, "%s", two_digits((int)mars.hoursUntilNextSol));
    snprintf(d2, sizeof d2, "%s",
             two_digits((int)fmodf(mars.secondsUntilNextSol / ROVER_SCREEN_MINUTES_DIVISOR, ROVER_SCREEN_MINUTES_MODULUS)));
    append(ROVER_SCREEN_HOURS_LABEL);
    append(d1);
    append(ROVER_SCREEN_TIME_COLON);
    append(d2);

    // original: // satelite imagery coverage
    append(ROVER_SCREEN_COVERAGE_LABEL);
    append(mars.isEarthVisible ? ROVER_SCREEN_COVERAGE_YES : ROVER_SCREEN_COVERAGE_NO);
    roverScreen.visibilityIndicator.enabled = !mars.isEarthVisible;

    // "" + Mathf.Round(...): a whole float prints with no decimals.
    int formattedDarkess = (int)mathf_round((1 - (mars.darkness) + ROVER_SCREEN_VISIBILITY_OFFSET) * ROVER_SCREEN_VISIBILITY_PERCENT);
    // Kept bug (plan 6.3, item 8): the source pads d1, not the darkness
    // number, when that is one digit long. d1 is already printed, and the
    // number is never shorter than 2 digits, so the line does nothing.
    gui_text_set(&roverScreen.visibilityIndicator, TextFormat("%s%d%s", ROVER_SCREEN_VISIBILITY_PREFIX,
                                                              formattedDarkess, ROVER_SCREEN_VISIBILITY_SUFFIX));

    return linedText;
}

// original: // Update is called once per frame
void rover_screen_update(void)
{
    const char *cleanText = rover_screen_print_screen();
    const char *glitchedText = cleanText;
    gui_text_set(&roverScreen.screenGUIText, glitchedText);

    // original: //Debug.Log ("cs "+mars.currentSol);
    roverScreen.lifeSupport = roverScreen.startingLifeSupport - mars.currentSol;
    if (roverScreen.lifeSupport <= ROVER_SCREEN_DEATH_THRESHOLD) { // original: // dies when next hungry not when it diminishes

        scene_load_name(ROVER_SCREEN_DEATH_SCENE);
    }
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
