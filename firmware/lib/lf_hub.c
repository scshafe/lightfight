#include "lf_hub.h"
#include "lf_conv.h"

static void emit(lf_hub *h, const lf_log_entry *e)
{
    if (h->log)
        h->log(h->log_ctx, e);
}

void lf_hub_init(lf_hub *h, const lf_rules_config *cfg, lf_hub_log_fn log, void *ctx)
{
    *h = (lf_hub){0};
    lf_rules_init(&h->rules, cfg);
    lf_evq_init(&h->events);
    for (int i = 0; i < LF_HUB_SENSORS; i++)
        lf_rx_init(&h->chan[i]);
    h->log = log;
    h->log_ctx = ctx;
}

void lf_hub_edge(lf_hub *h, int sensor, int level, uint32_t t_us)
{
    if (sensor >= 0 && sensor < LF_HUB_SENSORS)
        lf_rx_edge(&h->chan[sensor], level, t_us);
}

static void on_frame(lf_hub *h, int sensor, const lf_ir_frame *f, uint32_t now_ms, lf_hub_poll_result *r)
{
    h->frames_read[sensor]++;
    lf_hit hit = lf_rules_on_frame(&h->rules, now_ms, f);
    lf_log_entry e = {.t_ms = now_ms, .sensor = sensor, .frame = *f};
    if (hit.kind == LF_HIT_NONE) {
        e.kind = LF_LOG_DROP;
        e.reason = hit.reason;
        emit(h, &e);
        return;
    }
    lf_ble_event ev = {
        .kind = hit.kind == LF_HIT_DOWN ? LF_EVENT_DOWN : LF_EVENT_HIT,
        .shooter = hit.shooter,
        .seq = hit.seq,
        .damage = hit.damage,
        .health_after = hit.health_after,
        .t_ms = now_ms,
    };
    lf_evq_push(&h->events, &ev);
    r->hits++;
    if (hit.kind == LF_HIT_DOWN)
        r->downs++;
    e.kind = LF_LOG_HIT;
    e.damage = hit.damage;
    e.health = hit.health_after;
    emit(h, &e);
}

lf_hub_poll_result lf_hub_poll(lf_hub *h, uint32_t now_us)
{
    lf_hub_poll_result r = {0, 0};
    uint32_t now_ms = now_us / 1000;
    lf_rules_tick(&h->rules, now_ms);
    for (int s = 0; s < LF_HUB_SENSORS; s++) {
        lf_rx_chan *c = &h->chan[s];
        if (lf_rx_poll(c, now_us) != LF_RX_BURST)
            continue;
        unsigned from = 0, next = 0, found = 0;
        lf_ir_frame f;
        lf_ir_status st;
        while ((st = lf_rx_find_frame(c->d, c->n, from, &f, &next)) == LF_IR_OK) {
            found++;
            on_frame(h, s, &f, now_ms, &r);
            from = next;
        }
        if (!found) { /* something with enough edges to look like a burst, but no frame */
            lf_log_entry e = {.kind = LF_LOG_BAD, .t_ms = now_ms, .sensor = s, .status = st};
            emit(h, &e);
        }
    }
    return r;
}

void lf_hub_apply_config(lf_hub *h, const lf_ble_config *c)
{
    if (h->rules.active)
        return; /* a match is running: the config applies to the next one */
    lf_rules_config r = lf_conv_rules_config(c, h->rules.cfg.gap_ms);
    lf_rules_init(&h->rules, &r);
}

int lf_hub_command(lf_hub *h, uint8_t cmd, uint32_t now_ms)
{
    switch (cmd) {
    case LF_CMD_START:
        if (h->rules.active)
            return 0;
        lf_evq_init(&h->events);
        lf_rules_start(&h->rules, now_ms);
        return 1;
    case LF_CMD_STOP:
        if (!h->rules.active)
            return 0;
        lf_rules_stop(&h->rules);
        return 1;
    case LF_CMD_RESPAWN:
        if (!h->rules.active || h->rules.alive)
            return 0;
        h->rules.respawn_at_ms = now_ms; /* the next tick revives */
        return 1;
    default:
        return 0;
    }
}

void lf_hub_state(const lf_hub *h, uint8_t battery_pct, lf_ble_state *out) { lf_conv_state(&h->rules, battery_pct, out); }

void lf_hub_ack(lf_hub *h, uint16_t event_id) { lf_evq_ack(&h->events, event_id); }

void lf_hub_log_counters(lf_hub *h, uint32_t now_ms)
{
    for (int s = 0; s < LF_HUB_SENSORS; s++) {
        const lf_rx_chan *c = &h->chan[s];
        lf_log_entry e = {.kind = LF_LOG_RX, .t_ms = now_ms, .sensor = s,
                          .counters = {c->glitches, c->overflows, c->stuck, c->late, h->frames_read[s]}};
        emit(h, &e);
    }
}
