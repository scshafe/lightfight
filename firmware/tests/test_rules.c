#include <string.h>

#include "../lib/lf_rules.h"
#include "lf_test.h"

static lf_rules make(uint8_t player, uint8_t team, uint32_t t0)
{
    lf_rules r;
    lf_rules_config c = lf_rules_default_config(player, team);
    lf_rules_init(&r, &c);
    lf_rules_start(&r, t0);
    return r;
}

static lf_ir_frame shot(uint8_t shooter, uint8_t team, uint8_t dmg, uint8_t seq)
{
    lf_ir_frame f = {shooter, team, dmg, seq};
    return f;
}

static void test_trigger_rules(void)
{
    lf_rules r = make(3, 1, 0);
    lf_ir_frame f;
    CHECK(lf_rules_trigger(&r, 0, &f) == LF_SHOT_OK);
    CHECK(f.shooter == 3 && f.team == 1 && f.seq == 0);
    CHECK(r.ammo == 29);
    CHECK(lf_rules_trigger(&r, 100, &f) == LF_SHOT_RATE_LIMITED);
    CHECK(r.ammo == 29);
    CHECK(lf_rules_trigger(&r, 250, &f) == LF_SHOT_OK);
    CHECK(f.seq == 1);
    /* seq wraps at 16 */
    for (int i = 2; i < 16; i++)
        CHECK(lf_rules_trigger(&r, 250u * i + 250, &f) == LF_SHOT_OK);
    CHECK(f.seq == 15);
    CHECK(lf_rules_trigger(&r, 250u * 17, &f) == LF_SHOT_OK);
    CHECK(f.seq == 0);
}

static void test_ammo_and_reload(void)
{
    lf_rules r = make(1, 0, 0);
    lf_ir_frame f;
    uint32_t t = 0;
    CHECK(!lf_rules_reload(&r, t)); /* full */
    for (int i = 0; i < 30; i++, t += 250)
        CHECK(lf_rules_trigger(&r, t, &f) == LF_SHOT_OK);
    CHECK(lf_rules_trigger(&r, t, &f) == LF_SHOT_NO_AMMO);
    CHECK(lf_rules_reload(&r, t));
    CHECK(lf_rules_trigger(&r, t + 500, &f) == LF_SHOT_RELOADING);
    CHECK(!lf_rules_tick(&r, t + 1999).reload_finished);
    CHECK(lf_rules_tick(&r, t + 2000).reload_finished);
    CHECK(r.ammo == 30);
    CHECK(lf_rules_trigger(&r, t + 2000, &f) == LF_SHOT_OK);
}

static void test_inactive(void)
{
    lf_rules r = make(1, 0, 0);
    lf_ir_frame f;
    lf_rules_stop(&r);
    CHECK(lf_rules_trigger(&r, 0, &f) == LF_SHOT_INACTIVE);
    lf_ir_frame in = shot(9, 1, 0, 0);
    CHECK(lf_rules_on_frame(&r, 0, &in).reason == LF_IGNORE_INACTIVE);
}

static void test_hit_and_down_and_respawn(void)
{
    lf_rules r = make(1, 0, 0);
    lf_ir_frame a = shot(9, 1, 2, 0); /* damage 34 */
    lf_hit h = lf_rules_on_frame(&r, 100, &a);
    CHECK(h.kind == LF_HIT_DAMAGED && h.damage == 34 && h.health_after == 66);
    a.seq = 1;
    h = lf_rules_on_frame(&r, 400, &a);
    CHECK(h.kind == LF_HIT_DAMAGED && h.health_after == 32);
    a.seq = 2;
    h = lf_rules_on_frame(&r, 700, &a);
    CHECK(h.kind == LF_HIT_DOWN && h.damage == 32 && h.health_after == 0);
    CHECK(!r.alive);
    lf_ir_frame f;
    CHECK(lf_rules_trigger(&r, 800, &f) == LF_SHOT_DEAD);
    a.seq = 3;
    CHECK(lf_rules_on_frame(&r, 900, &a).reason == LF_IGNORE_DEAD);
    CHECK(!lf_rules_tick(&r, 10699).respawned);
    CHECK(lf_rules_tick(&r, 10700).respawned);
    CHECK(r.alive && r.health == 100 && r.ammo == 30);
}

static void test_dedupe(void)
{
    lf_rules r = make(1, 0, 0);
    lf_ir_frame a = shot(9, 1, 0, 5);
    CHECK(lf_rules_on_frame(&r, 1000, &a).kind == LF_HIT_DAMAGED);
    /* the same shot seen by other sensors, or repeated, within the window */
    CHECK(lf_rules_on_frame(&r, 1005, &a).reason == LF_IGNORE_DUPLICATE);
    CHECK(lf_rules_on_frame(&r, 1999, &a).reason == LF_IGNORE_DUPLICATE);
    CHECK(r.health == 90);
    /* a new shot of the same shooter has a new seq */
    a.seq = 6;
    CHECK(lf_rules_on_frame(&r, 1250, &a).kind == LF_HIT_DAMAGED);
    /* after the window the same (shooter, seq) is a new shot (seq wrapped) */
    a.seq = 5;
    CHECK(lf_rules_on_frame(&r, 2000, &a).kind == LF_HIT_DAMAGED);
    CHECK(r.health == 70);
    /* a different shooter with the same seq is a different shot */
    lf_ir_frame b = shot(10, 1, 0, 5);
    CHECK(lf_rules_on_frame(&r, 2001, &b).kind == LF_HIT_DAMAGED);
}

static void test_dedupe_survives_slot_wrap(void)
{
    lf_rules r = make(1, 0, 0);
    /* more distinct shots than slots within the window must not break
     * correctness for the most recent ones */
    for (int i = 0; i < 40; i++) {
        lf_ir_frame f = shot((uint8_t)(10 + i % 20), 1, 7, (uint8_t)(i & 15));
        lf_rules_on_frame(&r, 100u + i, &f);
    }
    lf_ir_frame last = shot((uint8_t)(10 + 39 % 20), 1, 7, 39 & 15);
    CHECK(lf_rules_on_frame(&r, 150, &last).reason == LF_IGNORE_DUPLICATE);
}

static void test_self_and_friendly(void)
{
    lf_rules r = make(1, 2, 0);
    lf_ir_frame own = shot(1, 2, 0, 0);
    CHECK(lf_rules_on_frame(&r, 0, &own).reason == LF_IGNORE_SELF);
    lf_ir_frame mate = shot(7, 2, 0, 0);
    CHECK(lf_rules_on_frame(&r, 0, &mate).reason == LF_IGNORE_FRIENDLY);
    CHECK(r.health == 100);

    lf_rules_config c = lf_rules_default_config(1, 2);
    c.friendly_fire = true;
    lf_rules_init(&r, &c);
    lf_rules_start(&r, 0);
    CHECK(lf_rules_on_frame(&r, 0, &mate).kind == LF_HIT_DAMAGED);
}

static void test_clock_wrap(void)
{
    uint32_t t = 0xFFFFFF00u;
    lf_rules r = make(1, 0, t);
    lf_ir_frame f;
    CHECK(lf_rules_trigger(&r, t, &f) == LF_SHOT_OK);
    CHECK(lf_rules_trigger(&r, t + 100, &f) == LF_SHOT_RATE_LIMITED);
    CHECK(lf_rules_trigger(&r, t + 250, &f) == LF_SHOT_OK); /* wrapped past 0 */
    lf_ir_frame a = shot(9, 1, 0, 0);
    CHECK(lf_rules_on_frame(&r, t, &a).kind == LF_HIT_DAMAGED);
    CHECK(lf_rules_on_frame(&r, t + 500, &a).reason == LF_IGNORE_DUPLICATE);
    CHECK(lf_rules_on_frame(&r, t + 1000, &a).kind == LF_HIT_DAMAGED);
}

/* A frame that went over the air: shooter's gear -> IR timings -> target. */
static void test_end_to_end_with_codec(void)
{
    lf_rules shooter = make(4, 1, 0), target = make(5, 2, 0);
    lf_ir_frame out, in;
    uint16_t t[LF_IR_TIMINGS];
    CHECK(lf_rules_trigger(&shooter, 0, &out) == LF_SHOT_OK);
    CHECK(lf_ir_build_timings(&out, t) == LF_IR_OK);
    /* three sensors see the same shot */
    int hits = 0;
    for (int s = 0; s < 3; s++) {
        CHECK(lf_ir_parse_timings(t, LF_IR_TIMINGS, &in) == LF_IR_OK);
        hits += lf_rules_on_frame(&target, 10 + s, &in).kind == LF_HIT_DAMAGED;
    }
    CHECK(hits == 1);
    CHECK(target.health == 90);
}

int main(void)
{
    test_trigger_rules();
    test_ammo_and_reload();
    test_inactive();
    test_hit_and_down_and_respawn();
    test_dedupe();
    test_dedupe_survives_slot_wrap();
    test_self_and_friendly();
    test_clock_wrap();
    test_end_to_end_with_codec();
    LF_DONE("test_rules");
}
