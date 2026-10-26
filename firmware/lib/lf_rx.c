#include "lf_rx.h"

void lf_rx_init(lf_rx_chan *c)
{
    *c = (lf_rx_chan){0};
    c->level = 1;
}

void lf_rx_edge(lf_rx_chan *c, int level, uint32_t t_us)
{
    level = level ? 1 : 0;
    if (c->active && c->level == 1 && t_us - c->edge_us >= LF_RX_IDLE_US) {
        /* the burst ended but nobody polled before the next one began */
        c->late++;
        c->active = 0;
    }
    if (!c->active) {
        if (level == 0) { /* a mark begins */
            c->active = 1;
            c->n = 0;
            c->level = 0;
            c->edge_us = t_us;
        }
        return;
    }
    if (level == c->level) { /* two edges the same way: one was missed, so the burst is untrustworthy */
        c->glitches++;
        c->active = 0;
        c->level = 1;
        return;
    }
    if (c->n == LF_RX_MAX_DURATIONS) {
        c->overflows++;
        c->active = 0;
        c->level = level;
        return;
    }
    uint32_t dur = t_us - c->edge_us;
    c->d[c->n++] = dur > 65535u ? 65535u : (uint16_t)dur;
    c->edge_us = t_us;
    c->level = level;
}

lf_rx_poll_result lf_rx_poll(lf_rx_chan *c, uint32_t now_us)
{
    if (!c->active)
        return LF_RX_NOTHING;
    uint32_t quiet = now_us - c->edge_us;
    if (c->level == 1 && quiet >= LF_RX_IDLE_US) {
        c->active = 0;
        return LF_RX_BURST;
    }
    if (c->level == 0 && quiet >= LF_RX_STUCK_US) {
        c->stuck++;
        c->active = 0;
        c->level = 0; /* wait for the rising edge before starting again */
    }
    return LF_RX_NOTHING;
}

lf_ir_status lf_rx_find_frame(const uint16_t *d, unsigned n, unsigned from, lf_ir_frame *f,
                              unsigned *next)
{
    if (n < from + LF_IR_TIMINGS)
        return LF_IR_ERR_LENGTH;
    lf_ir_status best = LF_IR_ERR_TIMING;
    uint32_t tol = LF_IR_HEADER_MARK_US * LF_IR_TOLERANCE_PERCENT / 100;
    for (unsigned i = from; i + LF_IR_TIMINGS <= n; i += 2) {
        if (d[i] < LF_IR_HEADER_MARK_US - tol || d[i] > LF_IR_HEADER_MARK_US + tol)
            continue;
        lf_ir_status s = lf_ir_parse_timings(d + i, LF_IR_TIMINGS, f);
        if (s == LF_IR_OK) {
            *next = i + LF_IR_TIMINGS - 1; /* the trailer mark; a next header follows its space */
            *next += *next % 2;            /* keep marks on even indexes */
            return s;
        }
        best = s;
    }
    return best;
}
