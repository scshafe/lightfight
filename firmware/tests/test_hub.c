#include <stdlib.h>
#include <string.h>

#include "../lib/lf_hub.h"
#include "../lib/lf_log.h"
#include "lf_test.h"
#include "rx_sim.h"

typedef struct {
    lf_log_entry e[4096];
    unsigned n;
} capture;

static void grab(void *ctx, const lf_log_entry *e)
{
    capture *c = ctx;
    if (c->n < 4096)
        c->e[c->n++] = *e;
}

static unsigned count(const capture *c, lf_log_kind k)
{
    unsigned n = 0;
    for (unsigned i = 0; i < c->n; i++)
        n += c->e[i].kind == k;
    return n;
}

static void feed_sensor(lf_hub *h, int sensor, lf_ir_frame f, uint32_t t0, int jitter, uint32_t *end)
{
    uint16_t t[LF_IR_TIMINGS];
    lf_ir_build_timings(&f, t);
    sim_edge e[64];
    unsigned n = 0;
    *end = sim_frame(e, &n, t, t0, jitter);
    for (unsigned i = 0; i < n; i++)
        lf_hub_edge(h, sensor, e[i].level, e[i].t_us);
}

static void idle_until(lf_hub *h, uint32_t from_us, uint32_t to_us, lf_hub_poll_result *total)
{
    for (uint32_t t = from_us; t <= to_us; t += 1000) {
        lf_hub_poll_result r = lf_hub_poll(h, t);
        total->hits += r.hits;
        total->downs += r.downs;
    }
}

static lf_hub make(capture *cap, uint8_t player, uint8_t team)
{
    lf_hub h;
    lf_rules_config c = lf_rules_default_config(player, team);
    lf_hub_init(&h, &c, grab, cap);
    lf_hub_command(&h, LF_CMD_START, 0);
    return h;
}

/* One shot seen by all six sensors, each a little differently timed: one hit. */
static void test_six_sensors_one_hit(void)
{
    srand(3);
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    lf_ir_frame f = {9, 1, 2, 5}; /* 34 damage */
    uint32_t end = 0;
    for (int s = 0; s < LF_HUB_SENSORS; s++)
        feed_sensor(&h, s, f, 100000 + (uint32_t)(s * 120), 30, &end);
    lf_hub_poll_result r = {0, 0};
    idle_until(&h, end, end + 20000, &r);
    CHECK(r.hits == 1);
    CHECK(h.rules.health == 66);
    CHECK(count(&cap, LF_LOG_HIT) == 1);
    CHECK(count(&cap, LF_LOG_DROP) == LF_HUB_SENSORS - 1); /* the other sensors: duplicates */
    for (unsigned i = 0; i < cap.n; i++)
        if (cap.e[i].kind == LF_LOG_DROP)
            CHECK(cap.e[i].reason == LF_IGNORE_DUPLICATE);
    CHECK(h.events.count == 1);
    for (int s = 0; s < LF_HUB_SENSORS; s++)
        CHECK(h.frames_read[s] == 1);
}

/* 200 shots, each sensor sees it with probability 0.6: counted exactly for the
 * shots that at least one sensor saw, never twice. */
static void test_lossy_sensors_exact_count(void)
{
    srand(11);
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    /* heal so it never dies: a huge max health */
    h.rules.cfg.max_health = 60000;
    h.rules.health = 60000;
    unsigned seen_shots = 0;
    uint32_t t = 200000;
    lf_hub_poll_result r = {0, 0};
    for (int shot = 0; shot < 200; shot++) {
        lf_ir_frame f = {(uint8_t)(10 + shot % 5), 1, 0, (uint8_t)(shot & 15)};
        uint32_t end = t;
        int any = 0;
        for (int s = 0; s < LF_HUB_SENSORS; s++) {
            if (rand() % 100 < 60) {
                feed_sensor(&h, s, f, t + (uint32_t)(rand() % 500), 30, &end);
                any = 1;
            }
        }
        seen_shots += any;
        t += 300000;
        idle_until(&h, end, t - 1000, &r);
        if (!any)
            idle_until(&h, t - 300000, t - 1000, &r);
    }
    CHECK(r.hits == seen_shots);
    CHECK(count(&cap, LF_LOG_HIT) == seen_shots);
}

static void test_down_event_and_ble_roundtrip(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    uint32_t t = 100000, end;
    lf_hub_poll_result r = {0, 0};
    for (int i = 0; i < 3; i++) { /* 34 + 34 + 34 > 100 */
        feed_sensor(&h, 0, (lf_ir_frame){9, 1, 2, (uint8_t)i}, t, 0, &end);
        idle_until(&h, end, end + 10000, &r);
        t += 300000;
    }
    CHECK(r.hits == 3 && r.downs == 1);
    CHECK(h.events.count == 3);
    const lf_ble_event *last = lf_evq_pending(&h.events, 2);
    CHECK(last && last->kind == LF_EVENT_DOWN && last->health_after == 0 && last->shooter == 9);
    /* what the phone would receive */
    uint8_t wire[LF_BLE_EVENT_LEN];
    lf_ble_event back;
    CHECK(lf_ble_encode_event(last, wire) == LF_BLE_EVENT_LEN);
    CHECK(lf_ble_decode_event(wire, sizeof wire, &back) && back.event_id == last->event_id);
    lf_ble_state st;
    lf_hub_state(&h, 77, &st);
    CHECK((st.flags & LF_STATE_ALIVE) == 0 && (st.flags & LF_STATE_ACTIVE) && st.health == 0 && st.battery_pct == 77);
    /* ack, then nothing is replayed */
    lf_hub_ack(&h, last->event_id);
    CHECK(h.events.count == 0);
}

static void test_commands_and_config(void)
{
    capture cap = {.n = 0};
    lf_hub h;
    lf_rules_config c = lf_rules_default_config(1, 0);
    lf_hub_init(&h, &c, grab, &cap);
    CHECK(!h.rules.active);
    lf_ble_config bc = {.player = 7, .team = 3, .friendly_fire = true, .max_health = 50, .respawn_ms = 5000,
                        .damage_table = {5, 10, 15, 20, 25, 30, 35, 40},
                        .mode = {{1, true, 10, 1000, 300}, {3, false, 4, 2000, 900}, {5, false, 2, 3000, 800}}};
    lf_hub_apply_config(&h, &bc);
    CHECK(h.rules.cfg.player == 7 && h.rules.cfg.team == 3 && h.rules.cfg.max_health == 50);
    CHECK(h.rules.cfg.friendly_fire && h.rules.cfg.damage_table[2] == 15);
    CHECK(h.rules.cfg.mode[1].magazine == 4 && h.rules.cfg.mode[2].damage_code == 5 && h.rules.cfg.mode[0].auto_reload);
    CHECK(lf_hub_command(&h, LF_CMD_START, 0) == 1);
    CHECK(h.rules.health == 50 && h.rules.active);
    CHECK(lf_hub_command(&h, LF_CMD_START, 0) == 0); /* already running */
    CHECK(lf_hub_command(&h, LF_CMD_DISABLE, 0) == 0 && lf_hub_command(&h, LF_CMD_ENABLE, 0) == 0); /* blaster-only */
    bc.player = 9;
    lf_hub_apply_config(&h, &bc); /* ignored mid-match */
    CHECK(h.rules.cfg.player == 7);
    CHECK(lf_hub_command(&h, LF_CMD_RESPAWN, 10) == 0); /* alive */
    CHECK(lf_hub_command(&h, LF_CMD_STOP, 20) == 1);
    CHECK(lf_hub_command(&h, LF_CMD_STOP, 20) == 0);
    CHECK(lf_hub_command(&h, LF_CMD_PING, 20) == 0);
}

static void test_frames_while_stopped_are_dropped(void)
{
    capture cap = {.n = 0};
    lf_hub h;
    lf_rules_config c = lf_rules_default_config(1, 0);
    lf_hub_init(&h, &c, grab, &cap);
    uint32_t end;
    lf_hub_poll_result r = {0, 0};
    feed_sensor(&h, 2, (lf_ir_frame){9, 1, 2, 5}, 100000, 0, &end);
    idle_until(&h, end, end + 10000, &r);
    CHECK(r.hits == 0 && h.events.count == 0);
    CHECK(count(&cap, LF_LOG_DROP) == 1 && cap.e[0].reason == LF_IGNORE_INACTIVE);
}

static void test_bad_burst_is_logged(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    /* a frame with a damaged bit: the CRC rejects it */
    lf_ir_frame f = {9, 1, 2, 5};
    uint16_t t[LF_IR_TIMINGS];
    lf_ir_build_timings(&f, t);
    t[2 + 2 * 10 + 1] = t[2 + 2 * 10 + 1] == LF_IR_SPACE0_US ? LF_IR_SPACE1_US : LF_IR_SPACE0_US;
    sim_edge e[64];
    unsigned n = 0;
    uint32_t end = sim_frame(e, &n, t, 100000, 0);
    for (unsigned i = 0; i < n; i++)
        lf_hub_edge(&h, 3, e[i].level, e[i].t_us);
    lf_hub_poll_result r = {0, 0};
    idle_until(&h, end, end + 10000, &r);
    CHECK(r.hits == 0 && h.rules.health == 100);
    CHECK(count(&cap, LF_LOG_BAD) == 1);
    CHECK(cap.e[0].sensor == 3 && cap.e[0].status == LF_IR_ERR_CRC);
}

static void test_log_format(void)
{
    char b[160];
    lf_log_entry e = {.kind = LF_LOG_HIT, .t_ms = 123456, .sensor = 2, .frame = {5, 1, 0, 9}, .damage = 10, .health = 90};
    CHECK(lf_log_format(&e, b, sizeof b) > 0);
    CHECK(!strcmp(b, "LFLOG t_ms=123456 ev=hit sensor=2 shooter=5 seq=9 dmg=10 health=90"));
    e = (lf_log_entry){.kind = LF_LOG_SHOT, .t_ms = 7, .frame = {5, 1, 0, 10}};
    lf_log_format(&e, b, sizeof b);
    CHECK(!strcmp(b, "LFLOG t_ms=7 ev=shot shooter=5 team=1 seq=10"));
    e = (lf_log_entry){.kind = LF_LOG_DROP, .t_ms = 8, .sensor = 1, .frame = {5, 1, 0, 9}, .reason = LF_IGNORE_DUPLICATE};
    lf_log_format(&e, b, sizeof b);
    CHECK(!strcmp(b, "LFLOG t_ms=8 ev=drop sensor=1 shooter=5 seq=9 reason=duplicate"));
    e = (lf_log_entry){.kind = LF_LOG_BAD, .t_ms = 9, .sensor = 4, .status = LF_IR_ERR_CRC};
    lf_log_format(&e, b, sizeof b);
    CHECK(!strcmp(b, "LFLOG t_ms=9 ev=bad sensor=4 why=crc"));
    e = (lf_log_entry){.kind = LF_LOG_RX, .t_ms = 10, .sensor = 0, .counters = {1, 2, 3, 4, 5}};
    lf_log_format(&e, b, sizeof b);
    CHECK(!strcmp(b, "LFLOG t_ms=10 ev=rx sensor=0 glitches=1 overflows=2 stuck=3 late=4 frames=5"));
    char tiny[8];
    CHECK(lf_log_format(&e, tiny, sizeof tiny) == 0);
}

/* the OR of the outputs over [from_ms, to_ms], sampled every millisecond */
static uint8_t outputs_between(lf_hub *h, uint32_t from_ms, uint32_t to_ms)
{
    uint8_t seen = 0;
    for (uint32_t t = from_ms; t <= to_ms; t++)
        seen |= lf_hub_outputs(h, t);
    return seen;
}

/* shoot the hub from one or more sensors, each seeing the frame skew_us late,
 * and poll as the firmware does (edges arrive in time order, never ahead of the
 * poll); return the outputs seen from the shot to 1.5 s after it */
static uint8_t shoot_and_feel(lf_hub *h, lf_ir_frame f, const int *sensors, const uint32_t *skew_us, int n,
                              uint32_t t0, unsigned *on_ms)
{
    uint16_t timings[LF_IR_TIMINGS];
    lf_ir_build_timings(&f, timings);
    sim_edge e[4][64];
    unsigned ne[4] = {0}, at[4] = {0};
    uint32_t last = 0;
    for (int i = 0; i < n; i++) {
        uint32_t end = sim_frame(e[i], &ne[i], timings, t0 + skew_us[i], 0);
        if (end > last)
            last = end;
    }
    uint8_t seen = 0;
    for (uint32_t t = t0; t <= last + 1500000; t += 1000) {
        for (int i = 0; i < n; i++)
            while (at[i] < ne[i] && e[i][at[i]].t_us <= t) {
                lf_hub_edge(h, sensors[i], e[i][at[i]].level, e[i][at[i]].t_us);
                at[i]++;
            }
        lf_hub_poll(h, t);
        uint8_t o = lf_hub_outputs(h, t / 1000);
        seen |= o;
        if (on_ms)
            *on_ms += o != 0;
    }
    return seen;
}

static void test_haptic_zones(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    uint32_t t = 100000000;
    /* a light body shot (10 hp) on the chest: the vest only */
    const int chest[] = {0};
    const uint32_t z[] = {0, 0};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 0}, chest, z, 1, t, NULL) == LF_OUT_VEST);
    /* a heavy shot (50 hp) on the right shoulder: vest */
    t += 3000000;
    const int shoulder[] = {2};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 3, 1}, shoulder, z, 1, t, NULL) == LF_OUT_VEST);
    CHECK(h.rules.health == 40);
    h.rules.health = 100;
    /* a shot only the headband front reads: the headband only */
    t += 3000000;
    const int head[] = {3};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 2}, head, z, 1, t, NULL) == LF_OUT_HEAD);
    /* the chest decodes it first, the headband 4 ms later: still a headshot */
    t += 3000000;
    const int both[] = {0, 4};
    const uint32_t skew[] = {0, 4000};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 3}, both, skew, 2, t, NULL) == LF_OUT_HEAD);
    /* the headband first, the chest after: also a headshot (not both motors) */
    t += 3000000;
    const int both2[] = {4, 1};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 4}, both2, skew, 2, t, NULL) == LF_OUT_HEAD);
    CHECK(h.rules.health == 70 && h.rules.alive); /* 90, then 80 and 70 for the two shots seen twice */
}

static void test_haptic_light_and_heavy_differ_in_length(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    const int chest[] = {0};
    const uint32_t z[] = {0};
    unsigned on_light = 0, on_heavy = 0;
    shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 0}, chest, z, 1, 100000000, &on_light);
    shoot_and_feel(&h, (lf_ir_frame){9, 1, 3, 1}, chest, z, 1, 103000000, &on_heavy);
    CHECK(on_light >= 59 && on_light <= 61);
    CHECK(on_heavy >= 249 && on_heavy <= 251);
}

static void test_haptic_down_and_respawn(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    const int chest[] = {0};
    const uint32_t z[] = {0};
    uint32_t t = 100000000;
    uint8_t seen = 0;
    for (int i = 0; i < 3; i++) { /* 3 x 34 hp: the third kills */
        seen = shoot_and_feel(&h, (lf_ir_frame){9, 1, 2, (uint8_t)i}, chest, z, 1, t, NULL);
        t += 3000000;
    }
    CHECK(!h.rules.alive);
    CHECK(seen == (LF_OUT_VEST | LF_OUT_HEAD | LF_OUT_BUZZER)); /* both motors and the buzzer */
    /* the respawn (10 s after the kill) buzzes both, with no buzzer */
    uint32_t ms = t / 1000;
    uint8_t at_respawn = 0;
    for (uint32_t u = ms; u < ms + 12000; u++) {
        lf_hub_poll(&h, u * 1000);
        at_respawn |= lf_hub_outputs(&h, u);
    }
    CHECK(h.rules.alive);
    CHECK(at_respawn == (LF_OUT_VEST | LF_OUT_HEAD));
    /* stop silences everything */
    lf_hub_command(&h, LF_CMD_STOP, ms + 13000);
    CHECK(outputs_between(&h, ms + 13000, ms + 13100) == 0);
}

/* The per-receiver map: every sensor has a name, a zone and a horizontal
 * facing; the vest ones are body, the two on the headband head, and the four
 * compass sides are all covered. */
static void test_sensor_table(void)
{
    CHECK(LF_HUB_SENSORS == 6);
    CHECK(lf_hub_sensor(-1) == NULL && lf_hub_sensor(LF_HUB_SENSORS) == NULL);
    unsigned facings = 0;
    for (int s = 0; s < LF_HUB_SENSORS; s++) {
        const lf_hub_sensor_desc *d = lf_hub_sensor(s);
        CHECK(d != NULL && d->name != NULL && d->name[0] != 0);
        CHECK(d->zone == lf_hub_sensor_zone(s));
        CHECK(d->facing <= LF_FACING_RIGHT);
        facings |= 1u << d->facing;
        for (int t = s + 1; t < LF_HUB_SENSORS; t++)
            CHECK(strcmp(d->name, lf_hub_sensor(t)->name) != 0);
    }
    CHECK(facings == 0xF);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_CHEST) == LF_ZONE_VEST);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_LEFT_SHOULDER) == LF_ZONE_VEST);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_RIGHT_SHOULDER) == LF_ZONE_VEST);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_BACK) == LF_ZONE_VEST);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_HEAD_FRONT) == LF_ZONE_HEAD);
    CHECK(lf_hub_sensor_zone(LF_SENSOR_HEAD_BACK) == LF_ZONE_HEAD);
    CHECK(lf_hub_sensor(LF_SENSOR_BACK)->facing == LF_FACING_REAR);
    CHECK(lf_hub_sensor(LF_SENSOR_CHEST)->facing == LF_FACING_FRONT);
}

/* A shot that only the back receiver reads counts once, buzzes the vest (a
 * body shot) and logs the sensor number 5; the receivers that read it are
 * recorded in the pending mask for a future shield. */
static void test_back_sensor_counts_as_body(void)
{
    capture cap = {.n = 0};
    lf_hub h = make(&cap, 1, 0);
    int only_back[1] = {LF_SENSOR_BACK};
    uint32_t skew[1] = {0};
    CHECK(shoot_and_feel(&h, (lf_ir_frame){9, 1, 0, 0}, only_back, skew, 1, 100000, NULL) == LF_OUT_VEST);
    CHECK(count(&cap, LF_LOG_HIT) == 1);
    CHECK(cap.e[0].sensor == LF_SENSOR_BACK);
    CHECK(h.rules.health == 90);

    /* the back and a headband receiver read one shot: a headshot, both in the mask */
    capture cap2 = {.n = 0};
    lf_hub h2 = make(&cap2, 1, 0);
    int two[2] = {LF_SENSOR_BACK, LF_SENSOR_HEAD_BACK};
    uint32_t skew2[2] = {0, 300};
    uint8_t seen = 0;
    {
        uint16_t timings[LF_IR_TIMINGS];
        lf_ir_frame f = {9, 1, 0, 1};
        lf_ir_build_timings(&f, timings);
        sim_edge e[2][64];
        unsigned ne[2] = {0}, at[2] = {0};
        uint32_t last = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t end = sim_frame(e[i], &ne[i], timings, 100000 + skew2[i], 0);
            if (end > last)
                last = end;
        }
        int mask_seen = 0;
        for (uint32_t t = 100000; t <= last + 200000; t += 1000) {
            for (int i = 0; i < 2; i++)
                while (at[i] < ne[i] && e[i][at[i]].t_us <= t) {
                    lf_hub_edge(&h2, two[i], e[i][at[i]].level, e[i][at[i]].t_us);
                    at[i]++;
                }
            lf_hub_poll(&h2, t);
            if (h2.pending.valid && h2.pending.sensors == ((1u << LF_SENSOR_BACK) | (1u << LF_SENSOR_HEAD_BACK)))
                mask_seen = 1;
            seen |= lf_hub_outputs(&h2, t / 1000);
        }
        CHECK(mask_seen);
    }
    CHECK(seen == LF_OUT_HEAD);
    CHECK(count(&cap2, LF_LOG_HIT) == 1);
}

int main(void)
{
    test_sensor_table();
    test_six_sensors_one_hit();
    test_back_sensor_counts_as_body();
    test_lossy_sensors_exact_count();
    test_down_event_and_ble_roundtrip();
    test_commands_and_config();
    test_haptic_zones();
    test_haptic_light_and_heavy_differ_in_length();
    test_haptic_down_and_respawn();
    test_frames_while_stopped_are_dropped();
    test_bad_burst_is_logged();
    test_log_format();
    LF_DONE("test_hub");
}
