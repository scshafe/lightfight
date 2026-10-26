#include <stdlib.h>
#include <string.h>

#include "../lib/lf_hub.h"
#include "../lib/lf_link.h"
#include "lf_test.h"

#ifndef BLE_VECTORS
#define BLE_VECTORS "../protocol/ble-vectors.csv"
#endif

/* ---- a fake device with settable state and events, to compare bytes ---- */
typedef struct {
    lf_ble_config config;
    int configs;
    uint8_t last_cmd;
    int cmds;
    lf_evq q;
    lf_ble_state state;
} fake;

static void f_config(void *c, const lf_ble_config *v) { fake *f = c; f->config = *v; f->configs++; }
static int f_cmd(void *c, uint8_t cmd, uint32_t now) { fake *f = c; (void)now; f->last_cmd = cmd; f->cmds++; return 1; }
static void f_ack(void *c, uint16_t id) { lf_evq_ack(&((fake *)c)->q, id); }
static void f_state(void *c, lf_ble_state *s) { *s = ((fake *)c)->state; }
static const lf_ble_event *f_pending(void *c, unsigned i) { return lf_evq_pending(&((fake *)c)->q, i); }

static lf_link make(fake *f)
{
    memset(f, 0, sizeof *f);
    lf_evq_init(&f->q);
    f->state = (lf_ble_state){3, 90, 25, 87};
    lf_link l;
    lf_link_ops ops = {f, f_config, f_cmd, f_ack, f_state, f_pending};
    lf_link_init(&l, &ops);
    return l;
}

static size_t from_hex(const char *s, uint8_t *out)
{
    size_t n = 0;
    for (; s[0] && s[1] && s[0] != '\n'; s += 2)
        out[n++] = (uint8_t)strtoul((char[3]){s[0], s[1], 0}, NULL, 16);
    return n;
}

static int split(char *line, char *f[], int max)
{
    int n = 0;
    for (char *p = strtok(line, ",\n"); p && n < max; p = strtok(NULL, ",\n"))
        f[n++] = p;
    return n;
}

/* The M3 vectors, through the link: writes that the phone sends are accepted
 * and reach the device; notifications the gear sends are those exact bytes. */
static void test_vectors(void)
{
    FILE *fp = fopen(BLE_VECTORS, "r");
    CHECK(fp != NULL);
    if (!fp)
        return;
    char line[256];
    int rows = 0;
    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *fld[32];
        int n = split(line, fld, 32);
        uint8_t wire[64];
        size_t wn = from_hex(fld[n - 1], wire);
        unsigned long v[32];
        for (int i = 1; i < n - 1; i++)
            v[i] = strtoul(fld[i], NULL, 10);
        fake f;
        lf_link l = make(&f);
        lf_link_set_connected(&l, 1);
        lf_notification note;
        if (!strcmp(fld[0], "config")) {
            CHECK(lf_link_write(&l, LF_CHR_CONFIG, wire, wn, 0) == LF_LINK_OK);
            CHECK(f.configs == 1 && f.config.player == v[1] && f.config.team == v[2] &&
                  f.config.max_health == v[4] && f.config.fire_interval_ms == v[8] &&
                  f.config.damage_table[7] == v[17]);
        } else if (!strcmp(fld[0], "command")) {
            CHECK(lf_link_write(&l, LF_CHR_COMMAND, wire, wn, 0) == LF_LINK_OK);
            CHECK(f.cmds == 1 && f.last_cmd == v[1]);
        } else if (!strcmp(fld[0], "ack")) {
            for (int i = 0; i < 3; i++) {
                lf_ble_event e = {.kind = LF_EVENT_HIT};
                lf_evq_push(&f.q, &e);
            }
            CHECK(lf_link_write(&l, LF_CHR_ACK, wire, wn, 0) == LF_LINK_OK);
            CHECK(f.q.count == (v[1] >= 3 && v[1] < 32768 ? 0u : 3u));
        } else if (!strcmp(fld[0], "state")) {
            f.state = (lf_ble_state){(uint8_t)v[1], (uint16_t)v[2], (uint16_t)v[3], (uint8_t)v[4]};
            CHECK(lf_link_next(&l, 0, LF_NOTIFY_STATE, &note) == 1);
            CHECK(note.which == LF_NOTIFY_STATE && note.len == wn && !memcmp(note.data, wire, wn));
            uint8_t rd[LF_BLE_STATE_LEN];
            CHECK(lf_link_read_state(&l, rd) == wn && !memcmp(rd, wire, wn));
        } else if (!strcmp(fld[0], "event")) {
            lf_ble_event e = {(uint8_t)v[1], (uint16_t)v[2], (uint8_t)v[3], (uint8_t)v[4], (uint8_t)v[5],
                              (uint16_t)v[6], (uint32_t)v[7]};
            f.q.ev[0] = e;
            f.q.count = 1;
            CHECK(lf_link_next(&l, 0, LF_NOTIFY_EVENTS, &note) == 1);
            CHECK(note.which == LF_NOTIFY_EVENTS && note.len == wn && !memcmp(note.data, wire, wn));
        } else {
            CHECK(!"unknown vector kind");
        }
        rows++;
    }
    fclose(fp);
    CHECK(rows >= 14);
}

static void test_bad_writes_change_nothing(void)
{
    fake f;
    lf_link l = make(&f);
    uint8_t cfg[LF_BLE_CONFIG_LEN] = {1};
    CHECK(lf_link_write(&l, LF_CHR_CONFIG, cfg, 5, 0) == LF_LINK_BAD_LENGTH);
    cfg[0] = 2;
    CHECK(lf_link_write(&l, LF_CHR_CONFIG, cfg, sizeof cfg, 0) == LF_LINK_BAD_VALUE);
    cfg[0] = 1; cfg[1] = 64;
    CHECK(lf_link_write(&l, LF_CHR_CONFIG, cfg, sizeof cfg, 0) == LF_LINK_BAD_VALUE);
    uint8_t cmd[2] = {1, 9};
    CHECK(lf_link_write(&l, LF_CHR_COMMAND, cmd, 2, 0) == LF_LINK_BAD_VALUE);
    CHECK(lf_link_write(&l, LF_CHR_COMMAND, cmd, 1, 0) == LF_LINK_BAD_LENGTH);
    uint8_t ack[3] = {7, 1, 0};
    CHECK(lf_link_write(&l, LF_CHR_ACK, ack, 3, 0) == LF_LINK_BAD_VALUE);
    CHECK(f.configs == 0 && f.cmds == 0);
}

static void push(fake *f, uint8_t seq)
{
    lf_ble_event e = {.kind = LF_EVENT_HIT, .shooter = 9, .seq = seq, .damage = 10, .health_after = 90};
    lf_evq_push(&f->q, &e);
}

static int next_event(lf_link *l, uint32_t now, lf_ble_event *out)
{
    lf_notification n;
    if (!lf_link_next(l, now, LF_NOTIFY_EVENTS, &n))
        return 0;
    return lf_ble_decode_event(n.data, n.len, out);
}

static void test_events_in_order_then_replay(void)
{
    fake f;
    lf_link l = make(&f);
    for (uint8_t i = 0; i < 5; i++)
        push(&f, i);
    lf_ble_event e;
    CHECK(!next_event(&l, 0, &e)); /* nobody connected */
    lf_link_set_connected(&l, 1);
    for (uint16_t id = 1; id <= 5; id++) {
        CHECK(next_event(&l, 0, &e) && e.event_id == id);
    }
    CHECK(!next_event(&l, 0, &e)); /* nothing new */
    push(&f, 5);
    CHECK(next_event(&l, 0, &e) && e.event_id == 6);

    /* link drops without an ack: the next phone gets all six again, in order */
    lf_link_set_connected(&l, 0);
    lf_link_set_connected(&l, 1);
    for (uint16_t id = 1; id <= 6; id++)
        CHECK(next_event(&l, 0, &e) && e.event_id == id);

    /* ack up to 4 while 5 and 6 are still queued: after the next reconnection
     * only 5 and 6 come */
    uint8_t ack[LF_BLE_ACK_LEN];
    lf_ble_encode_ack(4, ack);
    CHECK(lf_link_write(&l, LF_CHR_ACK, ack, sizeof ack, 0) == LF_LINK_OK);
    CHECK(!next_event(&l, 0, &e)); /* the stream had already passed them */
    lf_link_set_connected(&l, 0);
    lf_link_set_connected(&l, 1);
    CHECK(next_event(&l, 0, &e) && e.event_id == 5);
    CHECK(next_event(&l, 0, &e) && e.event_id == 6);
    CHECK(!next_event(&l, 0, &e));
}

static void test_ack_during_replay_moves_the_cursor(void)
{
    fake f;
    lf_link l = make(&f);
    for (uint8_t i = 0; i < 6; i++)
        push(&f, i);
    lf_link_set_connected(&l, 1);
    lf_ble_event e;
    CHECK(next_event(&l, 0, &e) && e.event_id == 1);
    CHECK(next_event(&l, 0, &e) && e.event_id == 2);
    uint8_t ack[LF_BLE_ACK_LEN];
    lf_ble_encode_ack(2, ack); /* the phone has 1 and 2 */
    lf_link_write(&l, LF_CHR_ACK, ack, sizeof ack, 0);
    CHECK(next_event(&l, 0, &e) && e.event_id == 3); /* not 5, not 1 */
    lf_ble_encode_ack(0, ack); /* a stale ack does nothing */
    lf_link_write(&l, LF_CHR_ACK, ack, sizeof ack, 0);
    CHECK(next_event(&l, 0, &e) && e.event_id == 4);
}

static void test_unsend_and_resubscribe(void)
{
    fake f;
    lf_link l = make(&f);
    push(&f, 0);
    push(&f, 1);
    lf_link_set_connected(&l, 1);
    lf_notification n;
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_EVENTS, &n) == 1);
    lf_link_unsend(&l); /* the radio was busy */
    lf_ble_event e;
    CHECK(next_event(&l, 0, &e) && e.event_id == 1); /* offered again */
    CHECK(next_event(&l, 0, &e) && e.event_id == 2);
    lf_link_resubscribed(&l, LF_NOTIFY_EVENTS);
    CHECK(next_event(&l, 0, &e) && e.event_id == 1);
    /* events are not sent to a phone that did not subscribe to them */
    lf_link_set_connected(&l, 1);
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_STATE, &n) == 1 && n.which == LF_NOTIFY_STATE);
}

static void test_queue_overflow_never_sends_garbage(void)
{
    fake f;
    lf_link l = make(&f);
    lf_link_set_connected(&l, 1);
    for (int i = 0; i < 10; i++)
        push(&f, (uint8_t)(i & 15));
    lf_ble_event e;
    for (int i = 0; i < 10; i++)
        CHECK(next_event(&l, 0, &e));
    for (int i = 0; i < 40; i++) /* the queue (32) overflows while nobody drains */
        push(&f, (uint8_t)(i & 15));
    unsigned got = 0;
    uint16_t last = 0;
    while (next_event(&l, 0, &e)) {
        CHECK(e.event_id != last);
        last = e.event_id;
        got++;
    }
    CHECK(got > 0 && got <= LF_EVQ_SLOTS);
}

static void test_state_pacing(void)
{
    fake f;
    lf_link l = make(&f);
    lf_notification n;
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_STATE, &n) == 0); /* not connected */
    lf_link_set_connected(&l, 1);
    CHECK(lf_link_next(&l, 100, LF_NOTIFY_STATE, &n) == 1); /* the first one */
    CHECK(lf_link_next(&l, 200, LF_NOTIFY_STATE, &n) == 0); /* unchanged */
    f.state.ammo = 24;
    CHECK(lf_link_next(&l, 300, LF_NOTIFY_STATE, &n) == 1); /* changed: at once */
    lf_ble_state s;
    CHECK(lf_ble_decode_state(n.data, n.len, &s) && s.ammo == 24);
    CHECK(lf_link_next(&l, 300 + LF_LINK_STATE_PERIOD_MS - 1, LF_NOTIFY_STATE, &n) == 0);
    CHECK(lf_link_next(&l, 300 + LF_LINK_STATE_PERIOD_MS, LF_NOTIFY_STATE, &n) == 1); /* keepalive */
    /* a failed send is offered again, even though the state has not changed */
    lf_link_unsend(&l);
    CHECK(lf_link_next(&l, 300 + LF_LINK_STATE_PERIOD_MS, LF_NOTIFY_STATE, &n) == 1);
    /* a new connection starts with the state */
    lf_link_set_connected(&l, 0);
    lf_link_set_connected(&l, 1);
    CHECK(lf_link_next(&l, 5000, LF_NOTIFY_STATE, &n) == 1);
}

static void test_events_before_state(void)
{
    fake f;
    lf_link l = make(&f);
    push(&f, 0);
    lf_link_set_connected(&l, 1);
    lf_notification n;
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_EVENTS | LF_NOTIFY_STATE, &n) == 1 && n.which == LF_NOTIFY_EVENTS);
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_EVENTS | LF_NOTIFY_STATE, &n) == 1 && n.which == LF_NOTIFY_STATE);
    CHECK(lf_link_next(&l, 0, LF_NOTIFY_EVENTS | LF_NOTIFY_STATE, &n) == 0);
}

/* ---- with the real hub: a hit goes out as an event, an ack frees it ---- */
static void hub_cmd_wrap(void *c, const lf_ble_config *cfg) { lf_hub_apply_config(c, cfg); }
static int hub_command_wrap(void *c, uint8_t cmd, uint32_t now) { return lf_hub_command(c, cmd, now); }
static void hub_ack_wrap(void *c, uint16_t id) { lf_hub_ack(c, id); }
static void hub_state_wrap(void *c, lf_ble_state *s) { lf_hub_state(c, 100, s); }
static const lf_ble_event *hub_pending_wrap(void *c, unsigned i) { return lf_evq_pending(&((lf_hub *)c)->events, i); }

static void test_with_the_real_hub(void)
{
    lf_hub h;
    lf_rules_config rc = lf_rules_default_config(1, 0);
    lf_hub_init(&h, &rc, NULL, NULL);
    lf_link l;
    lf_link_ops ops = {&h, hub_cmd_wrap, hub_command_wrap, hub_ack_wrap, hub_state_wrap, hub_pending_wrap};
    lf_link_init(&l, &ops);
    lf_link_set_connected(&l, 1);

    /* the phone configures and starts the match, as the server's join reply says */
    lf_ble_config bc = {.player = 7, .team = 2, .max_health = 100, .magazine = 30, .reload_ms = 2000,
                        .respawn_ms = 10000, .fire_interval_ms = 250, .damage_table = {10, 20, 34, 50, 75, 100, 5, 1}};
    uint8_t wire[LF_BLE_CONFIG_LEN];
    CHECK(lf_ble_encode_config(&bc, wire) == sizeof wire);
    CHECK(lf_link_write(&l, LF_CHR_CONFIG, wire, sizeof wire, 0) == LF_LINK_OK);
    uint8_t start[LF_BLE_COMMAND_LEN];
    lf_ble_encode_command(LF_CMD_START, start);
    CHECK(lf_link_write(&l, LF_CHR_COMMAND, start, sizeof start, 10) == LF_LINK_OK);
    CHECK(h.rules.cfg.player == 7 && h.rules.active);

    lf_ir_frame f = {9, 1, 2, 0};
    lf_hit hit = lf_rules_on_frame(&h.rules, 100, &f);
    CHECK(hit.kind == LF_HIT_DAMAGED);
    lf_ble_event ev = {.kind = LF_EVENT_HIT, .shooter = 9, .seq = 0, .damage = hit.damage, .health_after = hit.health_after, .t_ms = 100};
    lf_evq_push(&h.events, &ev);

    lf_notification n;
    CHECK(lf_link_next(&l, 100, LF_NOTIFY_EVENTS | LF_NOTIFY_STATE, &n) == 1 && n.which == LF_NOTIFY_EVENTS);
    lf_ble_event got;
    CHECK(lf_ble_decode_event(n.data, n.len, &got) && got.damage == 34 && got.health_after == 66);
    uint8_t ack[LF_BLE_ACK_LEN];
    lf_ble_encode_ack(got.event_id, ack);
    lf_link_write(&l, LF_CHR_ACK, ack, sizeof ack, 0);
    CHECK(h.events.count == 0);
    CHECK(lf_link_next(&l, 100, LF_NOTIFY_EVENTS | LF_NOTIFY_STATE, &n) == 1 && n.which == LF_NOTIFY_STATE);
    lf_ble_state st;
    CHECK(lf_ble_decode_state(n.data, n.len, &st) && st.health == 66 && (st.flags & LF_STATE_ALIVE));
}

/* the UUID bytes in the firmware are the ones written in docs/ble-protocol.md */
static void test_uuid_bytes(void)
{
    for (int x = 0; x <= 5; x++) {
        const uint8_t le[16] = {LF_UUID128_LE(x)};
        char text[40];
        snprintf(text, sizeof text, "4c46000%d-7f1a-4b6e-9c3d-2a5e8b1d0f47", x);
        /* parse the text form big-endian and compare reversed */
        uint8_t be[16];
        unsigned n = 0;
        for (const char *c = text; *c && n < 16; c++) {
            if (*c == '-')
                continue;
            be[n++] = (uint8_t)strtoul((char[3]){c[0], c[1], 0}, NULL, 16);
            c++;
        }
        CHECK(n == 16);
        for (int i = 0; i < 16; i++)
            CHECK(le[i] == be[15 - i]);
    }
}

int main(void)
{
    test_uuid_bytes();
    test_vectors();
    test_bad_writes_change_nothing();
    test_events_in_order_then_replay();
    test_ack_during_replay_moves_the_cursor();
    test_unsend_and_resubscribe();
    test_queue_overflow_never_sends_garbage();
    test_state_pacing();
    test_events_before_state();
    test_with_the_real_hub();
    LF_DONE("test_link");
}
