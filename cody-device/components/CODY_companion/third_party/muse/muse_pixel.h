/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "muse_state.h"

/*
 * Procedural pixel-art renderer for the Muse character.
 *
 * Muse is drawn on a coarse MUSE_PX_W x MUSE_PX_H grid with a small palette,
 * ordered dithering and hard outlines, then blown up to the screen with
 * nearest-neighbour blocks so the pixels stay chunky.
 */

#define MUSE_PX_W 64
#define MUSE_PX_H 64

typedef enum {
    MUSE_SYMBOL_NONE, MUSE_SYMBOL_QUESTION, MUSE_SYMBOL_EXCLAMATION,
    MUSE_SYMBOL_IDEA, MUSE_SYMBOL_THINKING, MUSE_SYMBOL_HEART,
    MUSE_SYMBOL_SWEAT, MUSE_SYMBOL_TENSION, MUSE_SYMBOL_SLEEP,
    MUSE_SYMBOL_COUNT
} muse_symbol_t;

typedef enum {
    MUSE_EYE_AUTO, MUSE_EYE_NORMAL, MUSE_EYE_WIDE, MUSE_EYE_HAPPY,
    MUSE_EYE_CROSS, MUSE_EYE_CLOSED, MUSE_EYE_COUNT
} muse_eye_style_t;

typedef enum {
    MUSE_MOUTH_AUTO, MUSE_MOUTH_SMILE, MUSE_MOUTH_ROUND, MUSE_MOUTH_THOUGHTFUL,
    MUSE_MOUTH_TALKING, MUSE_MOUTH_GRIN, MUSE_MOUTH_FLAT, MUSE_MOUTH_COUNT
} muse_mouth_style_t;

/* Artwork-only parameters; all values are validated by the Cody owner. */
typedef struct {
    uint8_t particle_count, particle_size;
    float particle_speed, particle_radius, particle_vertical_scale;
    float particle_twinkle_speed, particle_brightness;
    bool particle_custom_color;
    uint32_t particle_color_rgb;
    float motion, breathing_speed, body_x, body_y;
    float left_hand_x, left_hand_y, right_hand_x, right_hand_y;
    float gaze_x, gaze_y, eye_openness;
    muse_symbol_t symbol;
    int8_t symbol_offset_x, symbol_offset_y;
    uint8_t symbol_scale;
    float symbol_bob_speed;
    bool symbol_custom_color;
    uint32_t symbol_color_rgb;
    float pose_transition_seconds;
    muse_eye_style_t left_eye, right_eye;
    muse_mouth_style_t mouth;
    float talking_openness;
} muse_art_t;

typedef struct {
    muse_mode_t mode;
    float t;         /* seconds since boot */
    float mode_t;    /* seconds in current mode */
    float level;     /* 0..1 live audio level */
    float happy;     /* 0..1 pet reaction */
    float working;   /* 0 or 1 in THINKING: a call's Muse is busy on work it was asked
                      * for, nothing playing. A renderer that ignores it shows THINKING. */
    const muse_art_t *art; /* NULL selects original rendering defaults. */
} muse_pose_t;

/* Accent colour of a mode (for the surrounding UI), as 0xRRGGBB. */
uint32_t muse_pixel_accent(muse_mode_t mode);

/* Optional: the accent of the working pose (pose->working), as 0xRRGGBB, so
 * the UI around the avatar matches it. 0, or a renderer without it (the UI
 * links a weak default), keeps THINKING's accent. */
uint32_t muse_pixel_working_accent(void);

/* seed: Nonzero artistic PRNG seed, or zero for the original startup sequence.
 * The serialized owner renders before reading a frame after reset. */
void muse_pixel_reset_animation(uint32_t seed);

/* Render one frame into Muse's own MUSE_PX_W x MUSE_PX_H grid. */
void muse_pixel_render(const muse_pose_t *pose);

/* Size (square, in screen pixels) muse_pixel_scale() blows the grid up to. */
void muse_pixel_set_size(int px);

/*
 * Write screen pixels [x0, x1] x [y0, y1] of the blown-up frame as RGB565,
 * stride_px apart. Cheap enough to call per display strip, so the full-size
 * image never has to exist in RAM.
 */
void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);

/* Cody handoff: caller owns the output grid and both RGB565 palettes. */
#define MUSE_PX_FRAME_PALETTE 32
void muse_pixel_copy_frame(uint8_t cells[MUSE_PX_W * MUSE_PX_H],
                           uint16_t palette[MUSE_PX_FRAME_PALETTE],
                           uint16_t dim_palette[MUSE_PX_FRAME_PALETTE]);
