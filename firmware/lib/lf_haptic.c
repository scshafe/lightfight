#include "lf_haptic.h"

#define ALL (LF_OUT_VEST | LF_OUT_HEAD)

static const lf_haptic_step k_light[] = {{LF_OUT_HIT, 60}};
static const lf_haptic_step k_heavy[] = {{LF_OUT_HIT, 250}};
static const lf_haptic_step k_respawn[] = {{ALL, 80}, {0, 80}, {ALL, 80}};
static const lf_haptic_step k_down[] = {
    {ALL | LF_OUT_BUZZER, 300}, {0, 100}, {ALL | LF_OUT_BUZZER, 300}, {0, 100}, {ALL | LF_OUT_BUZZER, 600},
};

#define PATTERN(a) {a, sizeof a / sizeof a[0]}
static const lf_haptic_pattern k_patterns[] = {
    [LF_CUE_NONE] = {0, 0},
    [LF_CUE_HIT_LIGHT] = PATTERN(k_light),
    [LF_CUE_HIT_HEAVY] = PATTERN(k_heavy),
    [LF_CUE_RESPAWN] = PATTERN(k_respawn),
    [LF_CUE_DOWN] = PATTERN(k_down),
};

const lf_haptic_pattern *lf_haptic_pattern_for(lf_cue cue)
{
    return cue > LF_CUE_NONE && cue <= LF_CUE_DOWN ? &k_patterns[cue] : 0;
}

lf_cue lf_haptic_classify_hit(uint16_t full_damage, uint16_t max_health)
{
    return (uint32_t)full_damage * 10u >= (uint32_t)max_health * 3u ? LF_CUE_HIT_HEAVY : LF_CUE_HIT_LIGHT;
}

static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void lf_haptic_init(lf_haptic *h) { *h = (lf_haptic){.cue = LF_CUE_NONE}; }

void lf_haptic_stop(lf_haptic *h) { h->cue = LF_CUE_NONE; }

void lf_haptic_start(lf_haptic *h, lf_cue cue, uint8_t zone, uint32_t now_ms)
{
    const lf_haptic_pattern *p = lf_haptic_pattern_for(cue);
    lf_haptic_outputs(h, now_ms); /* a finished cue must not block this one */
    if (!p || cue < h->cue)
        return;
    h->cue = cue;
    h->step = 0;
    h->zone = zone;
    h->step_end_ms = now_ms + p->steps[0].ms;
}

uint8_t lf_haptic_outputs(lf_haptic *h, uint32_t now_ms)
{
    while (h->cue != LF_CUE_NONE) {
        const lf_haptic_pattern *p = &k_patterns[h->cue];
        if (!reached(now_ms, h->step_end_ms)) {
            uint8_t out = p->steps[h->step].out;
            if (out & LF_OUT_HIT)
                out = (uint8_t)((out & ~LF_OUT_HIT) | (h->zone == LF_ZONE_HEAD ? LF_OUT_HEAD : LF_OUT_VEST));
            return out;
        }
        if (++h->step >= p->n) {
            h->cue = LF_CUE_NONE;
            break;
        }
        h->step_end_ms += p->steps[h->step].ms;
    }
    return 0;
}
