#include <stdlib.h>
#include <string.h>

#include "../lib/lf_ir.h"
#include "../lib/lf_rx.h"
#include "lf_test.h"
#include "rx_sim.h"

static void timings(lf_ir_frame f, uint16_t t[LF_IR_TIMINGS]) { lf_ir_build_timings(&f, t); }

static void test_clean_frame(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    uint16_t t[LF_IR_TIMINGS];
    timings((lf_ir_frame){42, 1, 4, 3}, t);
    sim_edge e[64];
    unsigned n = 0;
    uint32_t end = sim_frame(e, &n, t, 1000000, 0);
    lf_ir_frame out[2];
    CHECK(sim_feed(&c, e, n, end, out, 2) == 1);
    CHECK(out[0].shooter == 42 && out[0].team == 1 && out[0].damage == 4 && out[0].seq == 3);
}

/* A receiver module stretches or shrinks marks and interrupt latency moves
 * the timestamps. The tolerance (25% of 560 us = 140 us) is the budget: with
 * every edge up to 60 us off (a mark can be 120 us wrong) every frame decodes. */
static void test_jitter(void)
{
    srand(7);
    for (int k = 0; k < 4000; k++) {
        lf_rx_chan c;
        lf_rx_init(&c);
        lf_ir_frame f = {(uint8_t)(k % 64), (uint8_t)(k % 8), (uint8_t)(k % 8), (uint8_t)(k % 16)};
        uint16_t t[LF_IR_TIMINGS];
        timings(f, t);
        sim_edge e[64];
        unsigned n = 0;
        uint32_t end = sim_frame(e, &n, t, 5000 + (uint32_t)k, 60);
        lf_ir_frame out[2];
        CHECK(sim_feed(&c, e, n, end, out, 2) == 1 && !memcmp(&out[0], &f, sizeof f));
    }
}

static void test_noise_before_and_after(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    uint16_t t[LF_IR_TIMINGS];
    timings((lf_ir_frame){9, 2, 1, 7}, t);
    sim_edge e[128];
    unsigned n = 0;
    /* a glitch 3 ms before the header (inside the idle window, so one burst),
     * and one 3 ms after the trailer */
    e[n++] = (sim_edge){100000, 0};
    e[n++] = (sim_edge){100080, 1};
    uint32_t end = sim_frame(e, &n, t, 103000, 0);
    e[n++] = (sim_edge){end + 3000, 0};
    e[n++] = (sim_edge){end + 3150, 1};
    lf_ir_frame out[2];
    CHECK(sim_feed(&c, e, n, end + 3150, out, 2) == 1);
    CHECK(out[0].shooter == 9 && out[0].seq == 7);
}

static void test_two_frames_back_to_back(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    sim_edge e[128];
    unsigned n = 0;
    uint16_t t[LF_IR_TIMINGS];
    timings((lf_ir_frame){1, 0, 0, 0}, t);
    uint32_t end = sim_frame(e, &n, t, 10000, 0);
    timings((lf_ir_frame){2, 0, 0, 1}, t);
    end = sim_frame(e, &n, t, end + 250000, 0);
    lf_ir_frame out[4];
    CHECK(sim_feed(&c, e, n, end, out, 4) == 2);
    CHECK(out[0].shooter == 1 && out[1].shooter == 2);
}

static void test_wrapping_clock(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    uint16_t t[LF_IR_TIMINGS];
    timings((lf_ir_frame){63, 7, 7, 15}, t);
    sim_edge e[64];
    unsigned n = 0;
    uint32_t end = sim_frame(e, &n, t, 0xFFFFFF00u, 0); /* the clock wraps mid-frame */
    lf_ir_frame out[2];
    CHECK(sim_feed(&c, e, n, end, out, 2) == 1 && out[0].shooter == 63);
}

static void test_saturation_and_overflow(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    /* output stuck low (receiver saturated by sunlight) */
    lf_rx_edge(&c, 0, 1000);
    CHECK(lf_rx_poll(&c, 1000 + LF_RX_STUCK_US - 1) == LF_RX_NOTHING);
    CHECK(lf_rx_poll(&c, 1000 + LF_RX_STUCK_US) == LF_RX_NOTHING);
    CHECK(c.stuck == 1 && !c.active);
    /* it recovers: the next frame is read normally */
    uint16_t t[LF_IR_TIMINGS];
    timings((lf_ir_frame){4, 4, 4, 4}, t);
    lf_rx_edge(&c, 1, 40000); /* the rising edge is ignored (no burst) */
    sim_edge e[64];
    unsigned n = 0;
    uint32_t end = sim_frame(e, &n, t, 100000, 0);
    lf_ir_frame out[2];
    CHECK(sim_feed(&c, e, n, end, out, 2) == 1);
    /* a flood of edges overflows the burst buffer, never memory */
    lf_rx_init(&c);
    for (uint32_t i = 0; i < 400; i++)
        lf_rx_edge(&c, (int)(i & 1), 1000 + i * 300);
    CHECK(c.overflows > 0 && c.n <= LF_RX_MAX_DURATIONS);
}

static void test_missed_edge_is_dropped(void)
{
    lf_rx_chan c;
    lf_rx_init(&c);
    lf_rx_edge(&c, 0, 1000);
    lf_rx_edge(&c, 1, 4000);
    lf_rx_edge(&c, 1, 5500); /* the falling edge was lost */
    CHECK(c.glitches == 1 && !c.active);
}

/* Sunlight, fluorescent ballast and remotes: random pulse trains must never
 * produce a frame. */
static void test_random_noise_never_decodes(void)
{
    srand(99);
    unsigned decoded = 0;
    for (int k = 0; k < 20000; k++) {
        lf_rx_chan c;
        lf_rx_init(&c);
        sim_edge e[160];
        unsigned n = 0;
        uint32_t now = 1000;
        unsigned pulses = 20 + (unsigned)(rand() % 50);
        for (unsigned i = 0; i < pulses; i++) {
            now += 100 + (uint32_t)(rand() % 3000);
            e[n++] = (sim_edge){now, 0};
            now += 100 + (uint32_t)(rand() % 3500);
            e[n++] = (sim_edge){now, 1};
        }
        lf_ir_frame out[2];
        decoded += sim_feed(&c, e, n, now, out, 2);
    }
    CHECK(decoded == 0);
}

/* Two blasters fire at the same time and their marks overlap: the receiver
 * sees the union of the two marks. Nearly always that fails the timing or CRC
 * check and nothing registers. A CRC-8 cannot reject everything: about one
 * garbled union in 256 that still has valid timing passes as a third frame
 * (measured: 4 in 20000 collisions, 0.02%; before the CRC started from 0xFF
 * it was 28, nearly all the all-zero frame). The bound below is 0.1%. */
static void test_collisions_rarely_invent_a_frame(void)
{
    srand(2024);
    unsigned decoded_real = 0, decoded_bogus = 0, total = 20000;
    for (unsigned k = 0; k < total; k++) {
        lf_ir_frame fa = {(uint8_t)(rand() % 64), (uint8_t)(rand() % 8), (uint8_t)(rand() % 8), (uint8_t)(rand() % 16)};
        lf_ir_frame fb = {(uint8_t)(rand() % 64), (uint8_t)(rand() % 8), (uint8_t)(rand() % 8), (uint8_t)(rand() % 16)};
        uint16_t ta[LF_IR_TIMINGS], tb[LF_IR_TIMINGS];
        timings(fa, ta);
        timings(fb, tb);
        sim_edge ea[64], eb[64];
        unsigned na = 0, nb = 0;
        uint32_t offset = (uint32_t)(rand() % 40000);
        uint32_t endA = sim_frame(ea, &na, ta, 10000, 0);
        uint32_t endB = sim_frame(eb, &nb, tb, 10000 + offset, 0);
        /* union of the marks: the output is low while either is */
        sim_edge e[256];
        unsigned n = 0;
        int depth = 0;
        unsigned ia = 0, ib = 0;
        while (ia < na || ib < nb) {
            sim_edge x;
            if (ib >= nb || (ia < na && ea[ia].t_us <= eb[ib].t_us))
                x = ea[ia++];
            else
                x = eb[ib++];
            int before = depth > 0;
            depth += x.level ? -1 : 1;
            int after = depth > 0;
            if (before != after)
                e[n++] = (sim_edge){x.t_us, after ? 0 : 1};
        }
        lf_rx_chan c;
        lf_rx_init(&c);
        lf_ir_frame out[3];
        unsigned got = sim_feed(&c, e, n, endA > endB ? endA : endB, out, 3);
        for (unsigned i = 0; i < got; i++) {
            if (!memcmp(&out[i], &fa, sizeof fa) || !memcmp(&out[i], &fb, sizeof fb))
                decoded_real++;
            else
                decoded_bogus++;
        }
    }
    printf("collisions: %u trials, %u real frames read, %u invented\n", total, decoded_real, decoded_bogus);
    CHECK(decoded_bogus * 1000 <= total);
}

int main(void)
{
    test_clean_frame();
    test_jitter();
    test_noise_before_and_after();
    test_two_frames_back_to_back();
    test_wrapping_clock();
    test_saturation_and_overflow();
    test_missed_edge_is_dropped();
    test_random_noise_never_decodes();
    test_collisions_rarely_invent_a_frame();
    LF_DONE("test_rx");
}
