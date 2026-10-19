#!/usr/bin/env python3
"""Check that every original code comment is kept in the C port.

Usage (from the repo root):
    python3 -I tools/check_comments.py extract/source_comments.json port/src

Plan requirement 8: each comment of the ported scripts goes into the C file
that ports the script, word for word, one "// original: <line>" per source
line. A comment that occurs n times in a script must occur n times in its C
file. Exits with status 1 and lists what is missing.
"""
import collections
import json
import os
import sys

# The C file that ports each script. None: the script is not ported.
C_FILE = {
    "Scripts/CamerFocusSwitcher.cs": "camera_focus.c",
    "Scripts/CameraFollowBounds.cs": None,  # attached to no object in any scene or prefab (plan section 6.2)
    "Scripts/CutsSceneScaleFixer.cs": "camera_focus.c",
    "Scripts/Earth.cs": "earth.c",
    "Scripts/EndGame.cs": "end_game.c",
    "Scripts/GUITextButton.cs": "title.c",
    "Scripts/InsideRocketTrigger.cs": "rocket.c",
    "Scripts/Intro.cs": "intro.c",
    "Scripts/LevelLoader.cs": "level_loader.c",
    "Scripts/Mars.cs": "mars.c",
    "Scripts/Morse.cs": "morse.c",
    "Scripts/NextSceneOnSpace.cs": "next_scene_on_space.c",
    "Scripts/Rock.cs": "rock.c",
    "Scripts/RockDragger.cs": "rock_dragger.c",
    "Scripts/RockGenerator.cs": "rock_generator.c",
    "Scripts/RockPlaceholder.cs": "rock_placeholder.c",
    "Scripts/Rocket.cs": "rocket.c",
    "Scripts/Rover.cs": "rover.c",
    "Scripts/RoverInsideTrigger.cs": "rover.c",
    "Scripts/RoverScreen.cs": "rover_screen.c",
    "Scripts/SnapToTarget.cs": "drag_target.c",
    "Scripts/Title.cs": "title.c",
    "Scripts/TopDownControls.cs": "player.c",
    "Scripts/VisibleOnTrigger.cs": "rocket.c",
    "Standard Assets/Particles/Legacy Particles/TimedObjectDestructor.js": "effects.c",
}

MARK = "// original: "


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    comments_json, src_dir = sys.argv[1], sys.argv[2]
    with open(comments_json, encoding="utf-8") as fh:
        files = json.load(fh)["files"]
    unknown = sorted(set(files) - set(C_FILE))
    if unknown:
        sys.exit("scripts with no entry in C_FILE: %s" % unknown)

    want = collections.defaultdict(collections.Counter)  # C file -> line -> times
    count = collections.Counter()
    for script, comments in sorted(files.items()):
        cfile = C_FILE[script]
        if cfile is None:
            print("skipped %s: %d comments, the script is not ported" % (script, len(comments)))
            continue
        count[cfile] += len(comments)
        for c in comments:
            for line in c["text"].split("\n"):
                want[cfile][MARK + line] += 1

    missing = 0
    for cfile in sorted(want):
        with open(os.path.join(src_dir, cfile), encoding="utf-8") as fh:
            # The kept comment runs from the marker to the end of the line.
            have = collections.Counter(line[line.index(MARK):].rstrip("\n") for line in fh if MARK in line)
        for line, n in sorted(want[cfile].items()):
            if have[line] < n:
                missing += 1
                print("MISSING in %s (%d of %d): %s" % (cfile, have[line], n, line))
        print("%s: %d comments" % (cfile, count[cfile]))
    print("%d comments checked, %d lines missing" % (sum(count.values()), missing))
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
