#include <string.h>

#include "../lib/lf_emitter.h"
#include "lf_test.h"

static void frame(uint16_t t[LF_IR_TIMINGS], uint8_t seq)
{
    lf_ir_frame f = {5, 1, 2, seq};
    lf_ir_build_timings(&f, t);
}

static void test_one_frame_budget(void)
{
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    /* header 3000 + 24 x 560 + trailer 560 of mark = 17000 us; half of it is LED on */
    CHECK(lf_emitter_led_on_us(t) == 8500);
    /* the worst frame length is within the hard cap */
    uint32_t total = 0;
    for (unsigned i = 0; i < LF_IR_TIMINGS; i++)
        total += t[i];
    CHECK(total <= LF_EMIT_MAX_FRAME_US);
}

static void test_rate_cap(void)
{
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    CHECK(lf_emitter_request(&e, 1000, t) == LF_EMIT_OK);
    CHECK(lf_emitter_request(&e, 1001, t) == LF_EMIT_BUSY);
    lf_emitter_done(&e);
    CHECK(lf_emitter_request(&e, 1100, t) == LF_EMIT_TOO_SOON);
    CHECK(lf_emitter_request(&e, 1249, t) == LF_EMIT_TOO_SOON);
    CHECK(lf_emitter_request(&e, 1250, t) == LF_EMIT_OK);
}

/* however often it is asked, over a long run the LED on-time per window and the
 * frame rate stay within the limits */
static void test_sustained_abuse(void)
{
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    unsigned sent = 0;
    uint32_t window_on[100] = {0};
    for (uint32_t now = 0; now < 60000; now++) { /* asked every millisecond for a minute */
        frame(t, (uint8_t)(now & 15));
        if (lf_emitter_request(&e, now, t) == LF_EMIT_OK) {
            sent++;
            window_on[now / 1000] += lf_emitter_led_on_us(t);
            lf_emitter_done(&e);
        }
    }
    CHECK(sent == 60000 / 250);
    for (int s = 0; s < 60; s++)
        CHECK(window_on[s] <= lim.max_led_on_us_window);
}

static void test_budget_limits_a_faster_cap(void)
{
    /* if the interval were loosened to 100 ms the budget still holds the line:
     * 8.5 ms per frame, 50 ms per second -> 5 frames per window */
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lim.min_interval_ms = 100;
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    unsigned ok = 0, over = 0;
    for (uint32_t now = 0; now < 1000; now += 100) {
        lf_emit_status s = lf_emitter_request(&e, now, t);
        if (s == LF_EMIT_OK) { ok++; lf_emitter_done(&e); }
        if (s == LF_EMIT_OVER_BUDGET) over++;
    }
    CHECK(ok == 5);
    CHECK(over == 5);
    /* the window slides: a second later there is room again */
    CHECK(lf_emitter_request(&e, 1100, t) == LF_EMIT_OK);
}

static void test_bad_frames(void)
{
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    /* a long continuous mark (LED stuck on) */
    t[0] = 60000;
    CHECK(lf_emitter_request(&e, 0, t) == LF_EMIT_BAD_FRAME);
    frame(t, 0);
    t[10] = 0;
    CHECK(lf_emitter_request(&e, 0, t) == LF_EMIT_BAD_FRAME);
    frame(t, 0);
    t[3] = 1000; /* a space off by a lot: not a frame that parses */
    CHECK(lf_emitter_request(&e, 0, t) == LF_EMIT_BAD_FRAME);
    /* nothing was recorded by the refusals */
    frame(t, 0);
    CHECK(lf_emitter_request(&e, 0, t) == LF_EMIT_OK);
}

static void test_fault_latches(void)
{
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    CHECK(lf_emitter_request(&e, 1000, t) == LF_EMIT_OK);
    CHECK(!lf_emitter_poll(&e, 1100)); /* still within the deadline */
    CHECK(lf_emitter_poll(&e, 1000 + 2 * 81)); /* the hardware never finished */
    CHECK(lf_emitter_faulted(&e));
    lf_emitter_done(&e); /* a late done does not clear it */
    CHECK(lf_emitter_request(&e, 5000, t) == LF_EMIT_FAULT);
    CHECK(lf_emitter_request(&e, 99999, t) == LF_EMIT_FAULT);
}

static void test_clock_wrap(void)
{
    lf_emitter e;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&e, &lim);
    uint16_t t[LF_IR_TIMINGS];
    frame(t, 0);
    uint32_t t0 = 0xFFFFFF80u;
    CHECK(lf_emitter_request(&e, t0, t) == LF_EMIT_OK);
    lf_emitter_done(&e);
    CHECK(lf_emitter_request(&e, t0 + 100, t) == LF_EMIT_TOO_SOON);
    CHECK(lf_emitter_request(&e, t0 + 250, t) == LF_EMIT_OK); /* past the wrap */
}

int main(void)
{
    test_one_frame_budget();
    test_rate_cap();
    test_sustained_abuse();
    test_budget_limits_a_faster_cap();
    test_bad_frames();
    test_fault_latches();
    test_clock_wrap();
    LF_DONE("test_emitter");
}
