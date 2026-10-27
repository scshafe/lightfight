#include <string.h>

#include "lf_rules.h"

/* a >= b on a wrapping 32-bit millisecond clock */
static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

lf_rules_config lf_rules_default_config(uint8_t player, uint8_t team)
{
    lf_rules_config c = {
        .player = player,
        .team = team,
        .damage_table = {10, 20, 34, 50, 75, 100, 5, 1},
        .max_health = 100,
        .respawn_ms = 10000,
        .gap_ms = 250,
        .dedupe_window_ms = 1000,
        .friendly_fire = false,
        .mode = {
            [LF_MODE_LIGHT] = {.damage_code = 0, .auto_reload = true, .magazine = 30, .reload_ms = 2000, .fire_interval_ms = 250},
            [LF_MODE_HEAVY] = {.damage_code = 3, .auto_reload = false, .magazine = 5, .reload_ms = 3000, .fire_interval_ms = 1500},
            [LF_MODE_SHOTGUN] = {.damage_code = 2, .auto_reload = false, .magazine = 2, .reload_ms = 3500, .fire_interval_ms = 1000},
        },
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
    r->disabled = false;
    r->health = r->cfg.max_health;
    for (int m = 0; m < LF_MODES; m++) {
        r->ammo[m] = r->cfg.mode[m].magazine;
        r->reloading[m] = false;
        r->fired[m] = false;
    }
    r->fired_any = false;
    r->last_mode = LF_MODE_LIGHT;
    memset(r->seen, 0, sizeof r->seen);
    r->seen_next = 0;
}

void lf_rules_stop(lf_rules *r)
{
    r->active = false;
    for (int m = 0; m < LF_MODES; m++)
        r->reloading[m] = false;
}

uint8_t lf_rules_reloading_mask(const lf_rules *r)
{
    uint8_t mask = 0;
    for (int m = 0; m < LF_MODES; m++)
        mask |= (uint8_t)(r->reloading[m] ? 1u << m : 0);
    return mask;
}

bool lf_rules_reload_mode(lf_rules *r, uint8_t mode, uint32_t now_ms)
{
    if (mode >= LF_MODES || !r->active || !r->alive || r->disabled || r->reloading[mode] ||
        r->ammo[mode] >= r->cfg.mode[mode].magazine)
        return false;
    r->reloading[mode] = true;
    r->reload_done_ms[mode] = now_ms + r->cfg.mode[mode].reload_ms;
    return true;
}

bool lf_rules_reload(lf_rules *r, uint32_t now_ms) { return lf_rules_reload_mode(r, r->last_mode, now_ms); }

bool lf_rules_reload_all(lf_rules *r, uint32_t now_ms)
{
    bool any = false;
    for (uint8_t m = 0; m < LF_MODES; m++)
        any |= lf_rules_reload_mode(r, m, now_ms);
    return any;
}

lf_shot_result lf_rules_trigger(lf_rules *r, uint8_t mode, uint32_t now_ms, lf_ir_frame *frame)
{
    if (mode >= LF_MODES)
        return LF_SHOT_BAD_MODE;
    const lf_mode_config *mc = &r->cfg.mode[mode];
    if (!r->active)
        return LF_SHOT_INACTIVE;
    if (!r->alive)
        return LF_SHOT_DEAD;
    if (r->disabled)
        return LF_SHOT_DISABLED;
    if (r->reloading[mode])
        return LF_SHOT_RELOADING;
    if (r->ammo[mode] == 0) {
        if (mc->auto_reload)
            lf_rules_reload_mode(r, mode, now_ms);
        return LF_SHOT_NO_AMMO;
    }
    if ((r->fired[mode] && !reached(now_ms, r->last_shot_ms[mode] + mc->fire_interval_ms)) ||
        (r->fired_any && !reached(now_ms, r->last_any_ms + r->cfg.gap_ms)))
        return LF_SHOT_RATE_LIMITED;
    r->ammo[mode]--;
    r->fired[mode] = true;
    r->last_shot_ms[mode] = now_ms;
    r->fired_any = true;
    r->last_any_ms = now_ms;
    r->last_mode = mode;
    frame->shooter = r->cfg.player;
    frame->team = r->cfg.team;
    frame->damage = mc->damage_code;
    frame->seq = r->seq;
    r->seq = (uint8_t)((r->seq + 1) & 15);
    if (r->ammo[mode] == 0 && mc->auto_reload)
        lf_rules_reload_mode(r, mode, now_ms);
    return LF_SHOT_OK;
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
        for (int m = 0; m < LF_MODES; m++)
            r->reloading[m] = false;
        r->respawn_at_ms = now_ms + r->cfg.respawn_ms;
        h.kind = LF_HIT_DOWN;
    } else {
        h.kind = LF_HIT_DAMAGED;
    }
    return h;
}

lf_tick_events lf_rules_tick(lf_rules *r, uint32_t now_ms)
{
    lf_tick_events e = {false, 0, false};
    if (!r->active)
        return e;
    for (int m = 0; m < LF_MODES; m++) {
        if (r->reloading[m] && reached(now_ms, r->reload_done_ms[m])) {
            r->reloading[m] = false;
            r->ammo[m] = r->cfg.mode[m].magazine;
            e.reload_finished = true;
            e.reloaded_mask |= (uint8_t)(1u << m);
        }
    }
    if (!r->alive && reached(now_ms, r->respawn_at_ms)) {
        r->alive = true;
        r->health = r->cfg.max_health;
        for (int m = 0; m < LF_MODES; m++)
            r->ammo[m] = r->cfg.mode[m].magazine;
        e.respawned = true;
    }
    return e;
}
