/* Test helper: turns IR frame timings into the edges a receiver module would
 * output, with jitter, glitches and several shooters overlapping. */
#ifndef RX_SIM_H
#define RX_SIM_H

#include <stdlib.h>

#include "../lib/lf_ir.h"
#include "../lib/lf_rx.h"

typedef struct {
    uint32_t t_us;
    int level; /* after the edge */
} sim_edge;

/* appends the edges of one frame starting at t0; returns the end time. Every
 * edge is moved by up to +-jitter_us, as a receiver stretches or shrinks marks */
static inline uint32_t sim_frame(sim_edge *e, unsigned *n, const uint16_t t[LF_IR_TIMINGS],
                                 uint32_t t0, int jitter_us)
{
    uint32_t now = t0;
    for (unsigned i = 0; i < LF_IR_TIMINGS; i++) {
        if (i % 2 == 0) { /* a mark: falling edge, then rising edge */
            int j1 = jitter_us ? (rand() % (2 * jitter_us + 1)) - jitter_us : 0;
            int j2 = jitter_us ? (rand() % (2 * jitter_us + 1)) - jitter_us : 0;
            e[(*n)++] = (sim_edge){now + (uint32_t)j1, 0};
            e[(*n)++] = (sim_edge){now + t[i] + (uint32_t)j2, 1};
        }
        now += t[i];
    }
    return now;
}

/* reads every frame out of the finished burst in c */
static inline unsigned sim_drain(const lf_rx_chan *c, lf_ir_frame *out, unsigned found, unsigned max_out)
{
    unsigned from = 0, next = 0;
    lf_ir_frame f;
    while (lf_rx_find_frame(c->d, c->n, from, &f, &next) == LF_IR_OK) {
        if (found < max_out)
            out[found++] = f;
        from = next;
    }
    return found;
}

/* feed edges (sorted by time) into a channel, polling before each edge as the
 * firmware's loop does every millisecond */
static inline unsigned sim_feed(lf_rx_chan *c, const sim_edge *e, unsigned n, uint32_t t_end_us,
                                lf_ir_frame *out, unsigned max_out)
{
    unsigned found = 0;
    for (unsigned i = 0; i < n; i++) {
        if (lf_rx_poll(c, e[i].t_us) == LF_RX_BURST)
            found = sim_drain(c, out, found, max_out);
        lf_rx_edge(c, e[i].level, e[i].t_us);
    }
    for (uint32_t t = t_end_us; t < t_end_us + 2 * LF_RX_IDLE_US; t += 500)
        if (lf_rx_poll(c, t) == LF_RX_BURST)
            found = sim_drain(c, out, found, max_out);
    return found;
}

#endif
