#include "../lib/lf_haptic.h"
#include "lf_test.h"

static unsigned total_ms(const lf_haptic_pattern *p)
{
    unsigned t = 0;
    for (unsigned i = 0; i < p->n; i++)
        t += p->steps[i].ms;
    return t;
}

/* every cue has a pattern of sane steps, and the cues are told apart */
static void test_patterns_are_data_that_make_sense(void)
{
    CHECK(lf_haptic_pattern_for(LF_CUE_NONE) == 0);
    CHECK(lf_haptic_pattern_for((lf_cue)99) == 0);
    for (lf_cue c = LF_CUE_HIT_LIGHT; c <= LF_CUE_DOWN; c++) {
        const lf_haptic_pattern *p = lf_haptic_pattern_for(c);
        CHECK(p && p->n >= 1 && p->steps);
        CHECK(total_ms(p) >= 40 && total_ms(p) <= 2000); /* felt, but not a nuisance */
        for (unsigned i = 0; i < p->n; i++) {
            CHECK(p->steps[i].ms > 0);
            CHECK((p->steps[i].out & 0x78) == 0); /* only known bits */
        }
        CHECK(p->steps[0].out != 0 && p->steps[p->n - 1].out != 0); /* starts and ends on a buzz */
    }
    const lf_haptic_pattern *l = lf_haptic_pattern_for(LF_CUE_HIT_LIGHT);
    const lf_haptic_pattern *hv = lf_haptic_pattern_for(LF_CUE_HIT_HEAVY);
    const lf_haptic_pattern *d = lf_haptic_pattern_for(LF_CUE_DOWN);
    const lf_haptic_pattern *r = lf_haptic_pattern_for(LF_CUE_RESPAWN);
    CHECK(total_ms(l) < total_ms(hv) && total_ms(hv) < total_ms(d)); /* light < heavy < killed */
    CHECK(r->n == 3 && r->steps[1].out == 0);                          /* a double tap */
    CHECK(d->n == 5);                                                  /* three pulses */
    /* hits use the zone that was hit; kill and respawn buzz both motors */
    CHECK(l->steps[0].out == LF_OUT_HIT && hv->steps[0].out == LF_OUT_HIT);
    CHECK((d->steps[0].out & (LF_OUT_VEST | LF_OUT_HEAD)) == (LF_OUT_VEST | LF_OUT_HEAD));
    CHECK((r->steps[0].out & (LF_OUT_VEST | LF_OUT_HEAD)) == (LF_OUT_VEST | LF_OUT_HEAD));
}

static void test_classify(void)
{
    CHECK(lf_haptic_classify_hit(10, 100) == LF_CUE_HIT_LIGHT);
    CHECK(lf_haptic_classify_hit(29, 100) == LF_CUE_HIT_LIGHT);
    CHECK(lf_haptic_classify_hit(30, 100) == LF_CUE_HIT_HEAVY);
    CHECK(lf_haptic_classify_hit(34, 100) == LF_CUE_HIT_HEAVY);
    CHECK(lf_haptic_classify_hit(50, 100) == LF_CUE_HIT_HEAVY);
    CHECK(lf_haptic_classify_hit(10, 20) == LF_CUE_HIT_HEAVY); /* relative to the health pool */
}

static void test_zone_and_timing(void)
{
    lf_haptic h;
    lf_haptic_init(&h);
    CHECK(lf_haptic_outputs(&h, 0) == 0);
    lf_haptic_start(&h, LF_CUE_HIT_LIGHT, LF_ZONE_VEST, 1000);
    CHECK(lf_haptic_outputs(&h, 1000) == LF_OUT_VEST);
    CHECK(lf_haptic_outputs(&h, 1059) == LF_OUT_VEST);
    CHECK(lf_haptic_outputs(&h, 1060) == 0);
    lf_haptic_start(&h, LF_CUE_HIT_HEAVY, LF_ZONE_HEAD, 2000); /* a headshot */
    CHECK(lf_haptic_outputs(&h, 2249) == LF_OUT_HEAD);
    CHECK(lf_haptic_outputs(&h, 2250) == 0);
    /* respawn: double tap on both */
    lf_haptic_start(&h, LF_CUE_RESPAWN, 0, 3000);
    const uint8_t both = LF_OUT_VEST | LF_OUT_HEAD;
    CHECK(lf_haptic_outputs(&h, 3000) == both);
    CHECK(lf_haptic_outputs(&h, 3080) == 0);
    CHECK(lf_haptic_outputs(&h, 3160) == both);
    CHECK(lf_haptic_outputs(&h, 3240) == 0);
    /* killed: 300 on, 100 off, 300 on, 100 off, 600 on, buzzer too */
    lf_haptic_start(&h, LF_CUE_DOWN, 0, 4000);
    CHECK(lf_haptic_outputs(&h, 4000) == (both | LF_OUT_BUZZER));
    CHECK(lf_haptic_outputs(&h, 4300) == 0);
    CHECK(lf_haptic_outputs(&h, 4400) == (both | LF_OUT_BUZZER));
    CHECK(lf_haptic_outputs(&h, 4700) == 0);
    CHECK(lf_haptic_outputs(&h, 4800) == (both | LF_OUT_BUZZER));
    CHECK(lf_haptic_outputs(&h, 5399) == (both | LF_OUT_BUZZER));
    CHECK(lf_haptic_outputs(&h, 5400) == 0);
    CHECK(lf_haptic_outputs(&h, 9000) == 0);
}

static void test_priority(void)
{
    lf_haptic h;
    lf_haptic_init(&h);
    lf_haptic_start(&h, LF_CUE_HIT_HEAVY, LF_ZONE_VEST, 0);
    lf_haptic_start(&h, LF_CUE_HIT_LIGHT, LF_ZONE_HEAD, 10); /* lower: ignored */
    CHECK(lf_haptic_outputs(&h, 20) == LF_OUT_VEST);
    lf_haptic_start(&h, LF_CUE_HIT_HEAVY, LF_ZONE_HEAD, 100); /* equal: restarts, new zone */
    CHECK(lf_haptic_outputs(&h, 110) == LF_OUT_HEAD);
    lf_haptic_start(&h, LF_CUE_DOWN, 0, 120); /* higher: replaces */
    CHECK(lf_haptic_outputs(&h, 130) & LF_OUT_BUZZER);
    lf_haptic_start(&h, LF_CUE_HIT_HEAVY, LF_ZONE_VEST, 140);
    lf_haptic_start(&h, LF_CUE_RESPAWN, 0, 150); /* below down: ignored while it plays */
    CHECK(lf_haptic_outputs(&h, 160) & LF_OUT_BUZZER);
    /* a finished cue does not block a lower one later, even if nobody polled */
    lf_haptic_start(&h, LF_CUE_HIT_LIGHT, LF_ZONE_VEST, 5000);
    CHECK(lf_haptic_outputs(&h, 5000) == LF_OUT_VEST);
    lf_haptic_stop(&h);
    CHECK(lf_haptic_outputs(&h, 5001) == 0);
}

static void test_clock_wrap(void)
{
    lf_haptic h;
    lf_haptic_init(&h);
    uint32_t t = 0xFFFFFFF0u;
    lf_haptic_start(&h, LF_CUE_HIT_HEAVY, LF_ZONE_VEST, t);
    CHECK(lf_haptic_outputs(&h, t + 100) == LF_OUT_VEST); /* now past 0 */
    CHECK(lf_haptic_outputs(&h, t + 250) == 0);
}

int main(void)
{
    test_patterns_are_data_that_make_sense();
    test_classify();
    test_zone_and_timing();
    test_priority();
    test_clock_wrap();
    LF_DONE("test_haptic");
}
