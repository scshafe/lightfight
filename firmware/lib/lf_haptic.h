/* Haptic cues: what the vest does to tell the player what happened. The
 * patterns are plain data; the player turns time into output bits and the
 * ESP-IDF glue only drives the pins. Tested on the host. */
#ifndef LF_HAPTIC_H
#define LF_HAPTIC_H

#include <stdbool.h>
#include <stdint.h>

/* In priority order: a cue never interrupts a higher one. */
typedef enum {
    LF_CUE_NONE = 0,
    LF_CUE_HIT_LIGHT,   /* a light hit: one short tick */
    LF_CUE_HIT_HEAVY,   /* a heavy hit: one long, strong buzz */
    LF_CUE_RESPAWN,     /* back in the game: a double tap on both motors */
    LF_CUE_DOWN         /* killed: three pulses and a long one on both motors, and the buzzer */
} lf_cue;

/* where a hit landed: the vest sensors (chest, shoulders) or the headband */
enum { LF_ZONE_VEST = 1, LF_ZONE_HEAD = 2 };

/* output bits */
enum { LF_OUT_VEST = 1, LF_OUT_HEAD = 2, LF_OUT_BUZZER = 4 };
/* in a pattern step: whichever zone was hit (a body shot buzzes the vest, a
 * headshot the headband) */
#define LF_OUT_HIT 0x80

typedef struct {
    uint8_t out;    /* LF_OUT_* bits, or LF_OUT_HIT; 0 is a pause */
    uint16_t ms;
} lf_haptic_step;

typedef struct {
    const lf_haptic_step *steps;
    unsigned n;
} lf_haptic_pattern;

/* The pattern of a cue (NULL for LF_CUE_NONE). */
const lf_haptic_pattern *lf_haptic_pattern_for(lf_cue cue);

/* A hit is heavy when its damage is at least 30% of the maximum health
 * (defaults: the 10 hp light shot is light; the 34 hp shotgun and the 50 hp
 * heavy shot are heavy). */
lf_cue lf_haptic_classify_hit(uint16_t full_damage, uint16_t max_health);

typedef struct {
    lf_cue cue;
    unsigned step;
    uint32_t step_end_ms;
    uint8_t zone;       /* LF_ZONE_* for LF_OUT_HIT */
} lf_haptic;

void lf_haptic_init(lf_haptic *h);
/* Start a cue now. A lower-priority cue than the one playing is ignored; an
 * equal or higher one replaces it. */
void lf_haptic_start(lf_haptic *h, lf_cue cue, uint8_t zone, uint32_t now_ms);
/* The output bits at now_ms (advances the pattern; 0 when finished). */
uint8_t lf_haptic_outputs(lf_haptic *h, uint32_t now_ms);
/* Stop at once (match stopped). */
void lf_haptic_stop(lf_haptic *h);

#endif
