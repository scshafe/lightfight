#include "lf_conv.h"

lf_rules_config lf_conv_rules_config(const lf_ble_config *c, uint32_t min_gap_ms)
{
    lf_rules_config r = lf_rules_default_config(c->player, c->team);
    r.friendly_fire = c->friendly_fire;
    r.max_health = c->max_health;
    r.respawn_ms = c->respawn_ms;
    r.gap_ms = min_gap_ms;
    for (int i = 0; i < 8; i++)
        r.damage_table[i] = c->damage_table[i];
    for (int m = 0; m < LF_MODES; m++) {
        r.mode[m].damage_code = c->mode[m].damage_code;
        r.mode[m].auto_reload = c->mode[m].auto_reload;
        r.mode[m].magazine = c->mode[m].magazine;
        r.mode[m].reload_ms = c->mode[m].reload_ms;
        r.mode[m].fire_interval_ms = c->mode[m].fire_interval_ms;
    }
    return r;
}

void lf_conv_state(const lf_rules *r, uint8_t battery_pct, lf_ble_state *out)
{
    uint8_t mask = lf_rules_reloading_mask(r);
    out->flags = (uint8_t)((r->active ? LF_STATE_ACTIVE : 0) | (r->alive ? LF_STATE_ALIVE : 0) |
                           (mask ? LF_STATE_RELOADING : 0) | (r->disabled ? LF_STATE_DISABLED : 0) |
                           (r->last_mode << LF_STATE_MODE_SHIFT));
    out->health = r->health;
    for (int m = 0; m < LF_MODES; m++)
        out->ammo[m] = r->ammo[m];
    out->reloading_mask = mask;
    out->battery_pct = battery_pct > 100 ? 100 : battery_pct;
}
