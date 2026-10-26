/* IR receive path: edges from one receiver module -> durations -> a frame.
 * Portable; the ESP-IDF glue only timestamps GPIO edges and feeds them here.
 * The receiver output is active low: level 0 is a mark. */
#ifndef LF_RX_H
#define LF_RX_H

#include <stdint.h>

#include "lf_ir.h"

#define LF_RX_MAX_DURATIONS 128 /* two frames and a little noise */
/* quiet time after the last rising edge that ends a burst (a frame's longest
 * gap is the 1.69 ms space) */
#define LF_RX_IDLE_US 6000u
/* a mark longer than this is the receiver saturated, not a frame */
#define LF_RX_STUCK_US 20000u

typedef struct {
    uint16_t d[LF_RX_MAX_DURATIONS];
    unsigned n;
    int active;     /* inside a burst */
    int level;      /* level after the last edge */
    uint32_t edge_us;
    /* counters, for the bench and field logs */
    uint32_t overflows, stuck, glitches, late;
} lf_rx_chan;

void lf_rx_init(lf_rx_chan *c);

/* An edge: level is the pin level after it. t_us is a free-running uint32
 * microsecond clock (wrap safe). Call from one context only. */
void lf_rx_edge(lf_rx_chan *c, int level, uint32_t t_us);

typedef enum { LF_RX_NOTHING = 0, LF_RX_BURST } lf_rx_poll_result;

/* Call often. When a burst has ended it returns LF_RX_BURST with the
 * durations in c->d[0..c->n) (valid until the next edge) and clears the
 * burst state. */
lf_rx_poll_result lf_rx_poll(lf_rx_chan *c, uint32_t now_us);

/* Looks for a frame in d[from..n) (noise may come before or after it): marks
 * sit at even indexes, so from must be even. On LF_IR_OK, *next is the index
 * just past the frame, where a search for a second frame in the same burst
 * continues. */
lf_ir_status lf_rx_find_frame(const uint16_t *d, unsigned n, unsigned from, lf_ir_frame *f,
                              unsigned *next);

#endif
