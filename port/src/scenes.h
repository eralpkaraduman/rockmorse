// The scenes, one Scene table entry each. main.c puts them in build order.
#ifndef ROCKMORSE_SCENES_H
#define ROCKMORSE_SCENES_H

#include "unity.h"

extern const Scene INTRO_SCENE;  // intro.c
extern const Scene TITLE_SCENE;  // title.c
extern const Scene STORY1_SCENE; // next_scene_on_space.c
extern const Scene STORY2_SCENE; // next_scene_on_space.c
extern const Scene GAME_SCENE;   // game.c
extern const Scene DEATH_SCENE;  // end_game.c
extern const Scene WIN_SCENE;    // end_game.c

// Builds an AudioSource from a values.h prefix, e.g. AUDIO_SOURCE(INTRO_SOUND).
// A source with ROLLOFF_CUSTOM also needs its curve, which only the 3D
// ones have (rock.c, effects.c).
#define AUDIO_SOURCE(p) ((AudioSource){ \
    .clip = p##_CLIP, .volume = p##_VOLUME, .pitch = p##_PITCH, \
    .loop = p##_LOOP, .playOnAwake = p##_PLAY_ON_AWAKE, .enabled = p##_ENABLED, \
    .spatialBlend = p##_SPATIAL_BLEND, .minDistance = p##_MIN_DISTANCE, .maxDistance = p##_MAX_DISTANCE, \
    .rolloffMode = p##_ROLLOFF_MODE, .dopplerLevel = p##_DOPPLER})

// Builds a GUIText from a values.h prefix, e.g. GUI_TEXT(TITLE_PLAY).
// GUIText colours are bytes in the dumps.
#define GUI_TEXT(p) ((GUIText){ \
    .text = p##_TEXT, \
    .position = {p##_VIEWPORT_POS_X, p##_VIEWPORT_POS_Y, p##_VIEWPORT_POS_Z}, \
    .fontSize = p##_FONT_SIZE, .anchor = p##_ANCHOR, .alignment = p##_ALIGNMENT, \
    .pixelOffset = {p##_PIXEL_OFFSET_X, p##_PIXEL_OFFSET_Y}, .lineSpacing = p##_LINE_SPACING, \
    .color = {p##_COLOR_R / 255.0f, p##_COLOR_G / 255.0f, p##_COLOR_B / 255.0f, p##_COLOR_A / 255.0f}, \
    .enabled = p##_ENABLED})

// Builds a full screen GUITexture from a values.h prefix. These four images
// have scale 0 in the dumps.
#define GUI_TEXTURE(p) ((GUITexture){ \
    .image = p##_TEXTURE, \
    .position = {p##_VIEWPORT_POS_X, p##_VIEWPORT_POS_Y, p##_VIEWPORT_POS_Z}, \
    .scale = {0, 0, 0}, \
    .pixelInset = {p##_PIXEL_INSET_X, p##_PIXEL_INSET_Y, p##_PIXEL_INSET_WIDTH, p##_PIXEL_INSET_HEIGHT}, \
    .color = {p##_COLOR_R, p##_COLOR_G, p##_COLOR_B, p##_COLOR_A}, \
    .enabled = p##_ENABLED})

// Builds a scene camera from a values.h prefix, e.g. CAMERA(INTRO_CAMERA).
#define CAMERA(p) ((UnityCamera){ \
    .position = {p##_POS_X, p##_POS_Y, p##_POS_Z}, .orthographicSize = p##_ORTHO_SIZE, \
    .background = {p##_BACKGROUND_R, p##_BACKGROUND_G, p##_BACKGROUND_B, p##_BACKGROUND_A}})

#endif
