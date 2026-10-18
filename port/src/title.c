// Title scene and its scripts, Title.cs and GUITextButton.cs. PLAY loads
// Story1 and SUPER DAMAGE opens the Super Damage site. Both buttons turn
// red under the mouse and play a sound.
#include "scenes.h"
#include "code_constants.h"

// GUITextButton.cs: a GUIText that reacts to the mouse.
typedef struct {
    GUIText *text;
    Rgba highlightedColor;
    Rgba normalColor;
    AudioSource *higlightSFX; // spelling as in the source
    AudioSource *clickSFX;
    void (*OnButtonMouseUp)(void);
    MouseTarget mouse;
} GUITextButton;

static UnityCamera camera;
static GUIText play, wr, title, ld, sd;
static AudioSource clickSound, highlightSound;
static GUITextButton playButton, sdButton;

// original: //http://docs.unity3d.com/ScriptReference/MonoBehaviour.html
static void OnMouseUp(GUITextButton *b)
{
    if (b->clickSFX) audio_source_play(b->clickSFX);

    b->OnButtonMouseUp();
}

static void OnMouseEnter(GUITextButton *b)
{
    if (b->higlightSFX) audio_source_play(b->higlightSFX);

    b->text->color = b->highlightedColor;
}

static void OnMouseExit(GUITextButton *b)
{
    b->text->color = b->normalColor;
}

// Unity sends the mouse events before Update.
static void gui_text_button_update(GUITextButton *b)
{
    MouseEvents e = gui_text_mouse_events(b->text, &b->mouse);
    if (e.enter) OnMouseEnter(b);
    if (e.exit) OnMouseExit(b);
    if (e.up) OnMouseUp(b);
}

// Title.cs
static void onOpenSDSite(void)
{
    application_open_url(TITLE_SITE_URL);
}

static void onPlay(void)
{
    scene_load_name(TITLE_PLAY_NEXT_SCENE);
}

static void Awake(void)
{
    sdButton.OnButtonMouseUp = onOpenSDSite;
    playButton.OnButtonMouseUp = onPlay;
}

// original: // Use this for initialization
static void Start(void)
{
}

static void start(void)
{
    camera = CAMERA(TITLE_CAMERA);
    play = GUI_TEXT(TITLE_PLAY);
    sd = GUI_TEXT(TITLE_SD);
    wr = GUI_TEXT(TITLE_WR);
    title = GUI_TEXT(TITLE_NAME);
    ld = GUI_TEXT(TITLE_LD);
    clickSound = AUDIO_SOURCE(TITLE_CLICK_SOUND);
    highlightSound = AUDIO_SOURCE(TITLE_HIGHLIGHT_SOUND);
    audio_source_awake(&clickSound);
    audio_source_awake(&highlightSound);
    // Both buttons use the scene's clickSound and highlightSound
    // (TITLE_*_BUTTON_CLICK_SFX, TITLE_*_BUTTON_HIGHLIGHT_SFX).
    playButton = (GUITextButton){
        .text = &play,
        .highlightedColor = {TITLE_PLAY_BUTTON_HIGHLIGHT_COLOR_R, TITLE_PLAY_BUTTON_HIGHLIGHT_COLOR_G,
                             TITLE_PLAY_BUTTON_HIGHLIGHT_COLOR_B, TITLE_PLAY_BUTTON_HIGHLIGHT_COLOR_A},
        .normalColor = {TITLE_PLAY_BUTTON_NORMAL_COLOR_R, TITLE_PLAY_BUTTON_NORMAL_COLOR_G,
                        TITLE_PLAY_BUTTON_NORMAL_COLOR_B, TITLE_PLAY_BUTTON_NORMAL_COLOR_A},
        .higlightSFX = &highlightSound,
        .clickSFX = &clickSound,
    };
    sdButton = (GUITextButton){
        .text = &sd,
        .highlightedColor = {TITLE_SD_BUTTON_HIGHLIGHT_COLOR_R, TITLE_SD_BUTTON_HIGHLIGHT_COLOR_G,
                             TITLE_SD_BUTTON_HIGHLIGHT_COLOR_B, TITLE_SD_BUTTON_HIGHLIGHT_COLOR_A},
        .normalColor = {TITLE_SD_BUTTON_NORMAL_COLOR_R, TITLE_SD_BUTTON_NORMAL_COLOR_G,
                        TITLE_SD_BUTTON_NORMAL_COLOR_B, TITLE_SD_BUTTON_NORMAL_COLOR_A},
        .higlightSFX = &highlightSound,
        .clickSFX = &clickSound,
    };
    Awake();
    Start();
}

// original: // Update is called once per frame
static void Update(void)
{
}

static void update(void)
{
    gui_text_button_update(&playButton);
    gui_text_button_update(&sdButton);
    Update();

    // Deliberate change (plan 6.4): the title had no keyboard path to Play.
    // Space or Enter (and gamepad A, which sends Space) presses PLAY as a
    // click would, with its sound. SUPER DAMAGE stays mouse only.
    if (input_get_key_down(KEY_SPACE) || input_get_key_down(KEY_ENTER)) OnMouseUp(&playButton);
}

static void draw(void)
{
    camera_clear(&camera);
    gui_text_draw(&play);
    gui_text_draw(&wr);
    gui_text_draw(&title);
    gui_text_draw(&ld);
    gui_text_draw(&sd);
}

const Scene TITLE_SCENE = {.name = "Title", .start = start, .update = update, .draw = draw};
