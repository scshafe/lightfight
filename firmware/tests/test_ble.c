#include <stdlib.h>
#include <string.h>

#include "../lib/lf_ble.h"
#include "ble_rows.h"
#include "lf_test.h"

#ifndef BLE_VECTORS
#define BLE_VECTORS "../protocol/ble-vectors.csv"
#endif

static size_t from_hex(const char *s, uint8_t *out)
{
    size_t n = 0;
    for (; s[0] && s[1] && s[0] != '\n'; s += 2)
        out[n++] = (uint8_t)strtoul((char[3]){s[0], s[1], 0}, NULL, 16);
    return n;
}

/* split a line on commas in place */
static int split(char *line, char *f[], int max)
{
    int n = 0;
    for (char *p = strtok(line, ",\n"); p && n < max; p = strtok(NULL, ",\n"))
        f[n++] = p;
    return n;
}

/* field by field: the structs have padding */
static bool same_config(const lf_ble_config *a, const lf_ble_config *b)
{
    for (int i = 0; i < LF_BLE_MODES; i++)
        if (a->mode[i].damage_code != b->mode[i].damage_code || a->mode[i].auto_reload != b->mode[i].auto_reload ||
            a->mode[i].magazine != b->mode[i].magazine || a->mode[i].reload_ms != b->mode[i].reload_ms ||
            a->mode[i].fire_interval_ms != b->mode[i].fire_interval_ms)
            return false;
    return a->player == b->player && a->team == b->team && a->friendly_fire == b->friendly_fire &&
           a->max_health == b->max_health && a->respawn_ms == b->respawn_ms &&
           !memcmp(a->damage_table, b->damage_table, 8);
}

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
        char *f[32];
        int n = split(line, f, 32);
        uint8_t want[64], got[64];
        size_t wn = from_hex(f[n - 1], want);
        unsigned long v[32];
        for (int i = 1; i < n - 1; i++)
            v[i] = strtoul(f[i], NULL, 10);
        if (!strcmp(f[0], "config")) {
            lf_ble_config m = row_config(v), d;
            CHECK(lf_ble_encode_config(&m, got) == wn && !memcmp(got, want, wn));
            CHECK(lf_ble_decode_config(want, wn, &d) && same_config(&m, &d));
        } else if (!strcmp(f[0], "state")) {
            lf_ble_state m = row_state(v), d;
            CHECK(lf_ble_encode_state(&m, got) == wn && !memcmp(got, want, wn));
            CHECK(lf_ble_decode_state(want, wn, &d) && m.flags == d.flags && m.health == d.health &&
                  !memcmp(m.ammo, d.ammo, sizeof m.ammo) && m.reloading_mask == d.reloading_mask &&
                  m.battery_pct == d.battery_pct);
        } else if (!strcmp(f[0], "event")) {
            lf_ble_event m = {(uint8_t)v[1], (uint16_t)v[2], (uint8_t)v[3], (uint8_t)v[4],
                              (uint8_t)v[5], (uint16_t)v[6], (uint32_t)v[7]}, d;
            CHECK(lf_ble_encode_event(&m, got) == wn && !memcmp(got, want, wn));
            CHECK(lf_ble_decode_event(want, wn, &d) && m.kind == d.kind && m.event_id == d.event_id &&
                  m.shooter == d.shooter && m.seq == d.seq && m.damage == d.damage &&
                  m.health_after == d.health_after && m.t_ms == d.t_ms);
        } else if (!strcmp(f[0], "ack")) {
            uint16_t id;
            CHECK(lf_ble_encode_ack((uint16_t)v[1], got) == wn && !memcmp(got, want, wn));
            CHECK(lf_ble_decode_ack(want, wn, &id) && id == v[1]);
        } else if (!strcmp(f[0], "command")) {
            uint8_t c;
            CHECK(lf_ble_encode_command((uint8_t)v[1], got) == wn && !memcmp(got, want, wn));
            CHECK(lf_ble_decode_command(want, wn, &c) && c == v[1]);
        } else {
            CHECK(!"unknown vector kind");
        }
        rows++;
    }
    fclose(fp);
    CHECK(rows >= 16);
}

static void test_rejects(void)
{
    uint8_t b[LF_BLE_CONFIG_LEN] = {0};
    lf_ble_config c;
    lf_ble_state s;
    lf_ble_event e;
    uint16_t id;
    uint8_t cmd;
    b[0] = 1;
    CHECK(lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c));
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN - 1, &c));
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN + 1, &c));
    b[0] = 2;
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c)); /* version */
    b[0] = 1; b[1] = 64;
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c)); /* player */
    b[1] = 0; b[3] = 2;
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c)); /* flag */
    b[3] = 0; b[16] = 8;
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c)); /* damage code */
    b[16] = 0; b[17 + 8] = 2;
    CHECK(!lf_ble_decode_config(b, LF_BLE_CONFIG_LEN, &c)); /* auto reload */
    b[17 + 8] = 0;
    uint8_t st[LF_BLE_STATE_LEN] = {1, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!lf_ble_decode_state(st, sizeof st, &s)); /* flag bit 6 */
    st[1] = 0x30;
    CHECK(!lf_ble_decode_state(st, sizeof st, &s)); /* mode 3 */
    st[1] = 0; st[10] = 8;
    CHECK(!lf_ble_decode_state(st, sizeof st, &s)); /* reloading mask */
    st[10] = 0; st[11] = 101;
    CHECK(!lf_ble_decode_state(st, sizeof st, &s));
    uint8_t ev[LF_BLE_EVENT_LEN] = {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!lf_ble_decode_event(ev, sizeof ev, &e)); /* kind */
    ev[1] = 1; ev[5] = 16;
    CHECK(!lf_ble_decode_event(ev, sizeof ev, &e)); /* seq */
    uint8_t ak[2] = {1, 0};
    CHECK(!lf_ble_decode_ack(ak, 2, &id));
    uint8_t cm[2] = {1, 7};
    CHECK(!lf_ble_decode_command(cm, 2, &cmd));
    cm[1] = 0;
    CHECK(!lf_ble_decode_command(cm, 2, &cmd));
    CHECK(!lf_ble_decode_command(cm, 1, &cmd));
    /* encoders refuse out-of-range fields */
    lf_ble_config bad = {.player = 64};
    CHECK(lf_ble_encode_config(&bad, b) == 0);
    lf_ble_event bev = {.kind = 9};
    CHECK(lf_ble_encode_event(&bev, ev) == 0);
    CHECK(lf_ble_encode_command(0, cm) == 0);
}

static lf_ble_event mk(uint8_t seq)
{
    lf_ble_event e = {.kind = LF_EVENT_HIT, .shooter = 9, .seq = seq, .damage = 10, .health_after = 90};
    return e;
}

static void test_queue(void)
{
    lf_evq q;
    lf_evq_init(&q);
    for (uint8_t i = 0; i < 5; i++) {
        lf_ble_event e = mk(i);
        CHECK(lf_evq_push(&q, &e) == i + 1u);
    }
    CHECK(lf_evq_pending(&q, 0)->event_id == 1);
    CHECK(lf_evq_pending(&q, 4)->event_id == 5);
    CHECK(lf_evq_pending(&q, 5) == NULL);
    lf_evq_ack(&q, 3); /* cumulative */
    CHECK(q.count == 2 && lf_evq_pending(&q, 0)->event_id == 4);
    lf_evq_ack(&q, 2); /* old ack: no effect */
    CHECK(q.count == 2);
    lf_evq_ack(&q, 5);
    CHECK(q.count == 0);
}

static void test_queue_overflow_and_wrap(void)
{
    lf_evq q;
    lf_evq_init(&q);
    for (int i = 0; i < 40; i++) {
        lf_ble_event e = mk(i & 15);
        lf_evq_push(&q, &e);
    }
    CHECK(q.count == LF_EVQ_SLOTS);
    CHECK(lf_evq_pending(&q, 0)->event_id == 9); /* 1..8 were dropped */
    /* ids wrap past 65535 and skip 0; ack still orders across it */
    lf_evq_init(&q);
    q.next_id = 65530;
    for (int i = 0; i < 10; i++) {
        lf_ble_event e = mk(i);
        lf_evq_push(&q, &e);
    }
    CHECK(lf_evq_pending(&q, 5)->event_id == 65535);
    CHECK(lf_evq_pending(&q, 6)->event_id == 1);
    lf_evq_ack(&q, 65535);
    CHECK(q.count == 4 && lf_evq_pending(&q, 0)->event_id == 1);
    lf_evq_ack(&q, 3);
    CHECK(q.count == 1);
}

int main(void)
{
    test_vectors();
    test_rejects();
    test_queue();
    test_queue_overflow_and_wrap();
    LF_DONE("test_ble");
}
