#include "lf_emitter.h"

/* wrap-safe: has now reached t */
static int reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

lf_emitter_limits lf_emitter_default_limits(void)
{
    lf_emitter_limits l = {250, 1000, 50000};
    return l;
}

void lf_emitter_init(lf_emitter *e, const lf_emitter_limits *lim)
{
    *e = (lf_emitter){0};
    e->lim = *lim;
}

uint32_t lf_emitter_led_on_us(const uint16_t t[LF_IR_TIMINGS])
{
    uint32_t marks = 0;
    for (unsigned i = 0; i < LF_IR_TIMINGS; i += 2)
        marks += t[i];
    return marks * LF_EMIT_CARRIER_DUTY_PERCENT / 100;
}

static int well_formed(const uint16_t t[LF_IR_TIMINGS])
{
    uint32_t total = 0;
    for (unsigned i = 0; i < LF_IR_TIMINGS; i++) {
        if (i % 2 == 0 && (t[i] == 0 || t[i] > LF_EMIT_MAX_MARK_US))
            return 0;
        total += t[i];
    }
    if (total > LF_EMIT_MAX_FRAME_US)
        return 0;
    /* it must be a frame this project would send: it parses, CRC and all */
    lf_ir_frame f;
    return lf_ir_parse_timings(t, LF_IR_TIMINGS, &f) == LF_IR_OK;
}

lf_emit_status lf_emitter_request(lf_emitter *e, uint32_t now_ms, const uint16_t t[LF_IR_TIMINGS])
{
    if (e->fault)
        return LF_EMIT_FAULT;
    if (e->busy)
        return LF_EMIT_BUSY;
    if (!well_formed(t))
        return LF_EMIT_BAD_FRAME;
    if (e->ever_started) {
        unsigned newest = (e->next + LF_EMIT_HISTORY - 1) % LF_EMIT_HISTORY;
        if (!reached(now_ms, e->start_ms[newest] + e->lim.min_interval_ms))
            return LF_EMIT_TOO_SOON;
    }
    uint32_t on = lf_emitter_led_on_us(t), used = on;
    for (unsigned i = 0; i < e->used; i++)
        if (!reached(now_ms, e->start_ms[i] + e->lim.window_ms))
            used += e->on_us[i];
    if (used > e->lim.max_led_on_us_window)
        return LF_EMIT_OVER_BUDGET;

    e->start_ms[e->next] = now_ms;
    e->on_us[e->next] = on;
    e->next = (e->next + 1) % LF_EMIT_HISTORY;
    if (e->used < LF_EMIT_HISTORY)
        e->used++;
    e->ever_started = 1;
    e->busy = 1;
    e->busy_since_ms = now_ms;
    /* the frame lasts at most LF_EMIT_MAX_FRAME_US; allow it twice over */
    e->busy_deadline_ms = now_ms + 2 * (LF_EMIT_MAX_FRAME_US / 1000 + 1);
    return LF_EMIT_OK;
}

void lf_emitter_done(lf_emitter *e) { e->busy = 0; }

int lf_emitter_poll(lf_emitter *e, uint32_t now_ms)
{
    if (e->busy && reached(now_ms, e->busy_deadline_ms)) {
        e->fault = 1;
        e->busy = 0;
    }
    return e->fault;
}

int lf_emitter_faulted(const lf_emitter *e) { return e->fault; }
