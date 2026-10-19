// LevelLoader.cs, on the Game scene's Level object. It loads each message
// from levels.json (tables.h), lays out one placeholder per dot and dash of
// the answer, checks the answer, and keeps the decoded letters that the map
// view shows. It also has the cheat key.
#include "game.h"
#include "tables.h"

LevelLoader levelLoader;

// m_PixelScale of the Joystix font in the 2016 build, from
// extract/build/font.json. values.h does not hold it yet.
#define JOYSTIX_PIXEL_SCALE 0.1f

// LevelLoader.cs private fields.
static float U;
static float V;
static Vector3 linePosStart;
static int lastLevelIndex;
static const char *currentAnswer; // set, never read, as in the source
static bool _lettersHidden;

// original: //public TextAsset[] levelAssets;
// original: //private Vector3 letterContainerInitialPos;
// original: //decoded
// original: /*
// original: 	private int decoded_wordIndex = 0;
// original: 	private int decoded_letterIndex = 0;
// original: 	private int decoded_letterSize = 0;
// original: 	private int decoded_totalChars = 0;
// original: 	private float decoded_totalLetterX = 0;
// original: 	*/
// original: //private List<Transform> decoded_letters = new List<Transform> ();

static bool renderLevel(int levelIndex);
static void printDecodedAnswerLetters(const char *decoded);

void level_loader_start(void)
{
    // The scene's field values.
    levelLoader = (LevelLoader){
        .currentLevel = LEVEL_LOADER_CURRENT_LEVEL,
        .position = {LEVEL_POS_X, LEVEL_POS_Y, LEVEL_POS_Z},
        .gap = {LEVEL_LOADER_GAP_X, LEVEL_LOADER_GAP_Y},
        .sentenceSize = {LEVEL_LOADER_SENTENCE_SIZE_X, LEVEL_LOADER_SENTENCE_SIZE_Y},
        .currentQuestion = LEVEL_LOADER_INITIAL_QUESTION,
    };
    U = 0;
    V = 0;
    linePosStart = (Vector3){0};
    lastLevelIndex = 0;
    currentAnswer = "";
    _lettersHidden = true;

    // Start: morse = new Morse(morseTextAsset.text). The table is tables.h.
    // original: //nextLevel ();
    // original: //letterContainerInitialPos = letterContainer.transform.position;
}

static void destroyCurrentLevel(void)
{

    U = 0;
    V = 0;

    for (int i = 0; i < levelLoader.placeholderCount; i++) {
        RockPlaceholder *ph = levelLoader.placeholders[i];
        // original: //rockGenerator.replace(ph.snappedRock);
        rock_placeholder_destroy(ph);
    }

    levelLoader.placeholderCount = 0;

    // original: // clear decoded letters
    // original: /*
    // original: 		decoded_wordIndex = 0;
    // original: 		decoded_letterIndex = 0;
    // original: 		decoded_letterSize = 0;
    // original: 		decoded_totalChars = 0;
    // original: 		decoded_totalLetterX = 0;
    // original: 
    // original: 		foreach(Transform letterTransform in decoded_letters ){
    // original: 			Destroy(letterTransform.gameObject);
    // original: 		}
    // original: 
    // original: 		decoded_letters = new List<Transform> ();
    // original: 		letterContainer.transform.position = letterContainerInitialPos;
    // original: 		*/
}

// original: // pass -1 for normal progression
bool level_loader_next_level(int forceLevel)
{

    if (forceLevel < 0) {
        levelLoader.currentLevel++;
    } else {
        levelLoader.currentLevel = forceLevel;
    }

    destroyCurrentLevel();
    return renderLevel(levelLoader.currentLevel);
}

static void beginNewSentence(int numLines)
{

    float sentenceHeight = (numLines + 1) * levelLoader.gap.y;

    levelLoader.sentenceSize.y = sentenceHeight;
    levelLoader.sentenceSize.x = 0;
    linePosStart.y = sentenceHeight / 2;
    linePosStart.z = levelLoader.position.z;
}

static void beginNewLine(int line, int numCodes)
{
    (void)line; // never read, as in the source
    U = 0;
    V++;
    (void)V; // counted, never read, as in the source



    float lineWidth = levelLoader.gap.x * numCodes;

    levelLoader.sentenceSize.x = fmaxf(levelLoader.sentenceSize.x, lineWidth); // Mathf.Max

    linePosStart.x = (-lineWidth / 2) + levelLoader.gap.x / 2;
    linePosStart.y -= levelLoader.gap.y;

    // original: //setDecodedNewLine ();
}

static void putPlaceholder(int code)
{
    Vector3 linePos = linePosStart;
    linePos.x += U * levelLoader.gap.x;
    U++;

    bool isGap = code == MORSE_CODE_GAP;

    // original: //setDecodedNextLetter(isGap);

    if (isGap) return;

    // Instantiate(placeholderPrefab, linePos, identity), parented to Level.
    RockPlaceholder *ph = rock_placeholder_instantiate(linePos);
    if (!ph) return;
    rock_placeholder_set_dash(ph, code == MORSE_CODE_DASH);

    if (levelLoader.placeholderCount < MAX_PLACEHOLDERS) levelLoader.placeholders[levelLoader.placeholderCount++] = ph;

}

static bool renderLevel(int levelIndex) // original: // returns true if there are more levels
{

    lastLevelIndex = LEVEL_COUNT - 1;



    const Level *levelData = &LEVELS[levelIndex];
    const char *q = levelData->question;
    const char *a = levelData->decoded;
    currentAnswer = a;
    (void)currentAnswer;
    const char *decoded_answer = levelData->decoded;

    levelLoader.currentQuestion = q;

    printDecodedAnswerLetters(a);

    if (levelIndex >= lastLevelIndex) return false; // original: // no more levels
    if (decoded_answer[0] == 0) return true; // decoded_answer.Length <= 0

    MorseWord encoded_answer[MORSE_MAX_WORDS];
    int words = morse_encode(decoded_answer, encoded_answer, MORSE_MAX_WORDS);

    beginNewSentence(words);

    // original: //int decodedLetterIndex = 0;

    for (int w = 0; w < words; w++) {

        int numCodes = encoded_answer[w].length;

        beginNewLine(w, numCodes);

        for (int l = 0; l < numCodes; l++) {

            // original: //Debug.Log("LT "+a[w]);

            int code = encoded_answer[w].codes[l];
            putPlaceholder(code);
        }

    }

    // original: //addLastLetter ();


    return true;

    // original: //timeMarkJustRendered = Time.time;
}

// original: /*
// original: 	private void setDecodedNextLetter(bool ended){
// original: 
// original: 		decoded_letterSize++;
// original: 
// original: 		if(ended){
// original: 			char c = currentAnswer[decoded_letterIndex];
// original: 			putLetter(c,decoded_letterSize-1);
// original: 			decoded_letterSize =0;
// original: 			decoded_letterIndex++;
// original: 		}
// original: 
// original: 	}
// original: 
// original: 	private void setDecodedNewLine(){
// original: 		decoded_wordIndex++;
// original: 		//decoded_letterIndex = 0;
// original: 		decoded_totalLetterX = 0;
// original: 	}
// original: 
// original: 	private void addLastLetter(){
// original: 		setDecodedNextLetter (true);
// original: 
// original: 		Vector3 pos = letterContainer.position;
// original: 		pos.x = -sentenceSize.x / 2;
// original: 		pos.y += (gap.y)/2;
// original: 		letterContainer.position = pos;
// original: 	}
// original: 
// original: 	private void putLetter(char l,int size){
// original: 
// original: 		Debug.Log (decoded_totalLetterX);
// original: 
// original: 		Debug.Log ("l "+l+
// original: 		           " u:"+decoded_letterIndex+
// original: 		           " v: "+decoded_wordIndex+
// original: 		           " s"+decoded_letterSize);
// original: 
// original: 		Vector3 letterPos = letterContainer.transform.position;
// original: 
// original: 		//float letterSize = 1;
// original: 		float letterWidth = (decoded_letterSize-1)*gap.x;
// original: 
// original: 		Transform letterTransform = Instantiate (letterPrefab, letterPos, Quaternion.identity) as Transform;
// original: 		letterTransform.parent = letterContainer;
// original: 		TextMesh tm = letterTransform.GetComponent<TextMesh>() as TextMesh;
// original: 		tm.text = ""+l;
// original: 		letterPos.x = decoded_totalLetterX + (letterWidth/2);
// original: 		letterPos.y = - decoded_wordIndex * gap.y;
// original: 		letterTransform.position = letterPos;
// original: 
// original: 		decoded_letters.Add (letterTransform);
// original: 
// original: 		decoded_totalLetterX += (letterWidth + gap.x);
// original: 
// original: 		decoded_totalChars++;
// original: 	}
// original: 
// original: 	*/

// One LetterPrefab instance: a TextMesh with its MeshRenderer off.
static TextMesh letter_prefab(Vector3 position)
{
    TextMesh tm = {
        .text = LETTER_TEXT,
        .transform = {.position = position, .scale = {LETTER_SCALE_X, LETTER_SCALE_Y, LETTER_SCALE_Z}},
        .fontSize = LETTER_FONT_SIZE,
        .characterSize = LETTER_CHARACTER_SIZE,
        .pixelScale = JOYSTIX_PIXEL_SCALE,
        .anchor = LETTER_ANCHOR,
        .alignment = LETTER_ALIGNMENT,
        .color = {LETTER_COLOR_R / 255.0f, LETTER_COLOR_G / 255.0f, LETTER_COLOR_B / 255.0f, LETTER_COLOR_A / 255.0f},
        .enabled = LETTER_MESH_RENDERER_ENABLED,
    };
    return tm;
}

static void printDecodedAnswerLetters(const char *decoded)
{

    // Destroy the old lines. Unity waits for the end of the frame, but
    // nothing draws them before then.
    levelLoader.decodedLineCount = 0;

    // decoded.Split(' '): an empty answer gives one empty line.
    int lines = 1;
    for (const char *s = decoded; *s; s++) lines += *s == LEVEL_LOADER_WORD_SEPARATOR;

    float yStart = (levelLoader.gap.y * lines) / 2 + (LEVEL_LOADER_DECODED_Y_OFFSET);

    Vector3 letterPos = {LETTERS_POS_X, LETTERS_POS_Y, LETTERS_POS_Z};
    letterPos.y = yStart;

    for (const char *line = decoded; levelLoader.decodedLineCount < MORSE_MAX_WORDS; ) {
        const char *end = strchr(line, LEVEL_LOADER_WORD_SEPARATOR);
        int length = end ? (int)(end - line) : (int)strlen(line);

        // original: //float xStart = ((line.Length + (line.Length - 1)) * gap.x) / 2;
        // original: //letterPos.x = xStart;

        TextMesh *tm = &levelLoader.decodedLines[levelLoader.decodedLineCount++];
        *tm = letter_prefab(letterPos);
        // original: // add gaps to letters
        tm->text[0] = 0;
        for (int c = 0; c < length; c++) {

            // tm.text += (c + " "): every letter, the last one too, gets a
            // space after it, so the centred line sits half a letter left.
            const char add[3] = {line[c], LEVEL_LOADER_LETTER_SEPARATOR[0], 0};
            strncat(tm->text, add, sizeof tm->text - strlen(tm->text) - 1);
        }

        // letterTransform.parent = letterContainer keeps the world position.

        letterPos.y -= levelLoader.gap.y;

        if (!end) break;
        line = end + 1;
    }
}

void level_loader_set_decoded_letters_hidden(bool hidden)
{
    if (_lettersHidden == hidden) return;
    _lettersHidden = hidden;

    for (int i = 0; i < levelLoader.decodedLineCount; i++) {
        levelLoader.decodedLines[i].enabled = !hidden;
    }

    float alpha = level_loader_check() ? LEVEL_LOADER_LETTERS_ALPHA_CORRECT : LEVEL_LOADER_LETTERS_ALPHA_WRONG;


    for (int i = 0; i < levelLoader.decodedLineCount; i++) {
        Rgba c = levelLoader.decodedLines[i].color;
        c.a = alpha;
        levelLoader.decodedLines[i].color = c;
    }

}

bool level_loader_check(void)
{

    if (levelLoader.placeholderCount <= 0) return false;

    bool correct = true;
    for (int i = 0; i < levelLoader.placeholderCount; i++) {
        const RockPlaceholder *holder = levelLoader.placeholders[i];

        if (rock_placeholder_is_correct(holder) == false) {
            correct = false;
        }
    }

    return correct;
}

static void cheat(void)
{

    for (int p = 0; p < levelLoader.placeholderCount; p++) {

        const RockPlaceholder *rp = levelLoader.placeholders[p];

        if (rock_placeholder_is_correct(rp)) continue;

        Rock *r = rock_generator_add_rock(rock_placeholder_is_dash(rp));
        if (!r) continue;
        Vector3 pos = rp->spriteRenderer.transform.position;
        pos.z = r->renderer.transform.position.z;
        rock_set_position(r, pos);
    }

}

void level_loader_update(void)
{

    // Deliberate change (plan 6.4): the cheat is "hold Ctrl, press C".
    // The original tested GetKeyDown(C) && GetKeyDown(LeftShift), so both
    // keys had to go down in the same frame, which almost never happened.
    // Shift now toggles the tutorial.
    bool ctrl = input_get_key(KEY_LEFT_CONTROL) || input_get_key(KEY_RIGHT_CONTROL);
    if (ctrl && input_get_key_down(KEY_C)) {
        cheat();
    }

}

// The decoded letters. Their MeshRenderer is on only in the map view.
void level_loader_draw(const UnityCamera *camera)
{
    for (int i = 0; i < levelLoader.decodedLineCount; i++) text_mesh_draw(camera, &levelLoader.decodedLines[i]);
}
