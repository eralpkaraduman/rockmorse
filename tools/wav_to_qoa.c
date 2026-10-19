// Writes a QOA copy of every WAV under port/assets into port/web/assets, at
// the same path. The web build ships these instead of the WAV files: QOA is
// about a fifth of the size, raylib reads it, and it keeps the exact frame
// count, so the loops stay gapless. The desktop build keeps the WAV files.
// Built with the desktop port (CMakeLists.txt). Run from the port folder:
//   ./build/wav_to_qoa assets web/assets
#include <stdio.h>
#include <string.h>
#include "raylib.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: wav_to_qoa <assets folder> <output folder>\n");
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    FilePathList files = LoadDirectoryFilesEx(argv[1], ".wav", true);
    size_t root = strlen(argv[1]);
    int failed = 0;
    for (unsigned int i = 0; i < files.count; i++) {
        const char *in = files.paths[i];
        char out[1024];
        snprintf(out, sizeof out, "%s%.*s.qoa", argv[2], (int)(strlen(in) - root - 4), in + root);
        MakeDirectory(GetDirectoryPath(out));
        Wave wave = LoadWave(in);
        bool ok = wave.frameCount > 0 && ExportWave(wave, out);
        // Read it back: same length, rate and channels.
        Wave back = LoadWave(out);
        ok = ok && back.frameCount == wave.frameCount && back.sampleRate == wave.sampleRate &&
             back.channels == wave.channels;
        printf("%s %s: %u frames\n", ok ? "ok    " : "FAILED", out, wave.frameCount);
        failed += !ok;
        UnloadWave(back);
        UnloadWave(wave);
    }
    UnloadDirectoryFiles(files);
    return failed ? 1 : 0;
}
