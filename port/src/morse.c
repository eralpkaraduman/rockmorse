// Morse.cs, a plain class that LevelLoader builds. It turns a sentence into
// one list of codes per word: dots, dashes, and a gap between letters.
// The table is morsecodes.json, which tables.h holds.
#include "game.h"
#include "tables.h"

// encodeChar: a linear search of the table for a row whose first or second
// field is the letter. A letter with no row gives no codes.
static int encodeChar(char letter, int *morse, int max)
{

    const char letterStr[2] = {letter, 0};
    int count = 0; // morse = new int[0]

    for (int i = 0; i < MORSE_COUNT; i++) {
        const char *chr = MORSE_TABLE[i].primary;
        const char *alt = MORSE_TABLE[i].alt;
        const char *morseStr = MORSE_TABLE[i].code;

        if (strcmp(letterStr, chr) == 0 || strcmp(letterStr, alt) == 0) {
            for (int chr_i = 0; morseStr[chr_i] && count < max; chr_i++) {

                int code = MORSE_CODE_GAP;
                if (morseStr[chr_i] == MORSE_TEXT_DASH[0]) code = MORSE_CODE_DASH;
                if (morseStr[chr_i] == MORSE_TEXT_DOT[0]) code = MORSE_CODE_DOT;

                morse[count++] = code;

            }
            return count;
        }
    }

    return count;
}

int morse_encode(const char *sentence, MorseWord *words, int maxWords)
{

    if (sentence[0] == 0) {
        return 0;
    }

    // sentence.Split(' '), one word at a time.
    int count = 0;
    for (const char *word = sentence; count < maxWords; ) {
        const char *end = strchr(word, MORSE_WORD_SEPARATOR);
        int length = end ? (int)(end - word) : (int)strlen(word);

        MorseWord *m_encodedWord = &words[count++];
        m_encodedWord->length = 0;
        for (int i_letter = 0; i_letter < length; i_letter++) {
            MorseWord *w = m_encodedWord;
            w->length += encodeChar(word[i_letter], w->codes + w->length, MORSE_MAX_CODES - w->length);
            if (w->length < MORSE_MAX_CODES) w->codes[w->length++] = MORSE_CODE_GAP;
        }
        // The last gap goes. An empty word (two spaces in a row) would make
        // C# throw here, on new int[-1]. No answer in levels.json has one.
        if (m_encodedWord->length > 0) m_encodedWord->length--;

        if (!end) break;
        word = end + 1;
    }

    return count;
}
