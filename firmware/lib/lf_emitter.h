/* The emitter's safety gate (docs/design.md, "Safety"). Whatever asks for a
 * transmission - the rules engine today, anything else tomorrow - goes through
 * lf_emitter_request, which refuses a waveform that is not a well-formed IR
 * frame, a rate above the cap, or more LED on-time than the budget allows. A
 * transmission that never ends latches a fault: the driver must then hold the
 * LED off until the board is reset. The hardware limits (series resistor,
 * gate pull-down) do not depend on this code; this is the second layer. */
#ifndef LF_EMITTER_H
#define LF_EMITTER_H

#include <stdint.h>

#include "lf_ir.h"

/* Carrier duty the budget assumes. The driver must not exceed it. */
#define LF_EMIT_CARRIER_DUTY_PERCENT 50u
/* A single mark longer than this is not part of any valid frame. */
#define LF_EMIT_MAX_MARK_US 3300u
/* Hard cap on one frame, header to trailer. */
#define LF_EMIT_MAX_FRAME_US 80000u

#define LF_EMIT_HISTORY 16

typedef struct {
    uint32_t min_interval_ms;       /* between two frame starts (default 250: 4/s) */
    uint32_t window_ms;             /* sliding window for the budget (default 1000) */
    uint32_t max_led_on_us_window;  /* LED on-time allowed per window (default 50000) */
} lf_emitter_limits;

typedef enum {
    LF_EMIT_OK = 0,
    LF_EMIT_BAD_FRAME,   /* not a 51-duration frame within the mark/length caps */
    LF_EMIT_TOO_SOON,    /* inside min_interval_ms of the last start */
    LF_EMIT_OVER_BUDGET, /* would exceed the on-time budget of the window */
    LF_EMIT_BUSY,        /* the previous transmission has not finished */
    LF_EMIT_FAULT        /* latched: a transmission overran; LED must stay off */
} lf_emit_status;

typedef struct {
    lf_emitter_limits lim;
    uint32_t start_ms[LF_EMIT_HISTORY];
    uint32_t on_us[LF_EMIT_HISTORY];
    unsigned next;
    unsigned used;
    int busy;
    uint32_t busy_since_ms;
    uint32_t busy_deadline_ms;
    int fault;
    int ever_started;
} lf_emitter;

lf_emitter_limits lf_emitter_default_limits(void);
void lf_emitter_init(lf_emitter *e, const lf_emitter_limits *lim);

/* LED on-time of one frame in microseconds, with the assumed carrier duty. */
uint32_t lf_emitter_led_on_us(const uint16_t t[LF_IR_TIMINGS]);

/* Ask to start sending t at now_ms. On LF_EMIT_OK the caller transmits at once
 * and must call lf_emitter_done when the hardware reports the end. */
lf_emit_status lf_emitter_request(lf_emitter *e, uint32_t now_ms, const uint16_t t[LF_IR_TIMINGS]);
void lf_emitter_done(lf_emitter *e);

/* Call from a periodic task. Returns 1 (and latches the fault) when a
 * transmission has run past its deadline; the caller then forces the LED off. */
int lf_emitter_poll(lf_emitter *e, uint32_t now_ms);
int lf_emitter_faulted(const lf_emitter *e);

#endif
