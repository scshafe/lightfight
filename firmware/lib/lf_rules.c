#include <string.h>

#include "lf_rules.h"

/* a >= b on a wrapping 32-bit millisecond clock */
static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

lf_rules_config lf_rules_default_config(uint8_t player, uint8_t team)
{
    lf_rules_config c = {
        .player = player,
        .team = team,
        .damage_code = 0,
        .damage_table = {10, 20, 34, 50, 75, 100, 5, 1},
        .max_health = 100,
        .magazine = 30,
        .reload_ms = 2000,
        .respawn_ms = 10000,
        .fire_interval_ms = 250,
        .dedupe_window_ms = 1000,
        .friendly_fire = false,
    };
    return c;
}

void lf_rules_init(lf_rules *r, const lf_rules_config *cfg)
{
    memset(r, 0, sizeof *r);
    r->cfg = *cfg;
}

void lf_rules_start(lf_rules *r, uint32_t now_ms)
{
    (void)now_ms;
    r->active = true;
    r->alive = true;
    r->health = r->cfg.max_health;
    r->ammo = r->cfg.magazine;
    r->reloading = false;
    r->fired = false;
    memset(r->seen, 0, sizeof r->seen);
    r->seen_next = 0;
}

void lf_rules_stop(lf_rules *r)
{
    r->active = false;
    r->reloading = false;
}

lf_shot_result lf_rules_trigger(lf_rules *r, uint32_t now_ms, lf_ir_frame *frame)
{
    if (!r->active)
        return LF_SHOT_INACTIVE;
    if (!r->alive)
        return LF_SHOT_DEAD;
    if (r->reloading)
        return LF_SHOT_RELOADING;
    if (r->ammo == 0)
        return LF_SHOT_NO_AMMO;
    if (r->fired && !reached(now_ms, r->last_shot_ms + r->cfg.fire_interval_ms))
        return LF_SHOT_RATE_LIMITED;
    r->ammo--;
    r->fired = true;
    r->last_shot_ms = now_ms;
    frame->shooter = r->cfg.player;
    frame->team = r->cfg.team;
    frame->damage = r->cfg.damage_code;
    frame->seq = r->seq;
    r->seq = (uint8_t)((r->seq + 1) & 15);
    return LF_SHOT_OK;
}

bool lf_rules_reload(lf_rules *r, uint32_t now_ms)
{
    if (!r->active || !r->alive || r->reloading || r->ammo >= r->cfg.magazine)
        return false;
    r->reloading = true;
    r->reload_done_ms = now_ms + r->cfg.reload_ms;
    return true;
}

static bool seen_before(lf_rules *r, const lf_ir_frame *f, uint32_t now_ms)
{
    for (unsigned i = 0; i < LF_DEDUPE_SLOTS; i++) {
        lf_dedupe_slot *s = &r->seen[i];
        if (s->used && s->shooter == f->shooter && s->seq == f->seq &&
            !reached(now_ms, s->at_ms + r->cfg.dedupe_window_ms))
            return true;
    }
    lf_dedupe_slot *s = &r->seen[r->seen_next];
    r->seen_next = (r->seen_next + 1) % LF_DEDUPE_SLOTS;
    s->shooter = f->shooter;
    s->seq = f->seq;
    s->at_ms = now_ms;
    s->used = true;
    return false;
}

static lf_hit ignored(lf_ignore_reason why, const lf_ir_frame *f)
{
    lf_hit h = {.kind = LF_HIT_NONE, .reason = why, .shooter = f->shooter, .seq = f->seq};
    return h;
}

lf_hit lf_rules_on_frame(lf_rules *r, uint32_t now_ms, const lf_ir_frame *f)
{
    if (!r->active)
        return ignored(LF_IGNORE_INACTIVE, f);
    if (f->shooter == r->cfg.player)
        return ignored(LF_IGNORE_SELF, f);
    if (seen_before(r, f, now_ms))
        return ignored(LF_IGNORE_DUPLICATE, f);
    if (!r->alive)
        return ignored(LF_IGNORE_DEAD, f);
    if (f->team == r->cfg.team && !r->cfg.friendly_fire)
        return ignored(LF_IGNORE_FRIENDLY, f);

    uint8_t dmg_full = r->cfg.damage_table[f->damage & 7];
    uint8_t dmg = dmg_full > r->health ? (uint8_t)r->health : dmg_full;
    r->health = (uint16_t)(r->health - dmg);
    lf_hit h = {.shooter = f->shooter, .seq = f->seq, .damage = dmg, .health_after = r->health};
    if (r->health == 0) {
        r->alive = false;
        r->reloading = false;
        r->respawn_at_ms = now_ms + r->cfg.respawn_ms;
        h.kind = LF_HIT_DOWN;
    } else {
        h.kind = LF_HIT_DAMAGED;
    }
    return h;
}

lf_tick_events lf_rules_tick(lf_rules *r, uint32_t now_ms)
{
    lf_tick_events e = {false, false};
    if (!r->active)
        return e;
    if (r->reloading && reached(now_ms, r->reload_done_ms)) {
        r->reloading = false;
        r->ammo = r->cfg.magazine;
        e.reload_finished = true;
    }
    if (!r->alive && reached(now_ms, r->respawn_at_ms)) {
        r->alive = true;
        r->health = r->cfg.max_health;
        r->ammo = r->cfg.magazine;
        e.respawned = true;
    }
    return e;
}
