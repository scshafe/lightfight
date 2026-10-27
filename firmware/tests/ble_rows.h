/* Reading protocol/ble-vectors.csv rows into the message structs. */
#ifndef BLE_ROWS_H
#define BLE_ROWS_H

#include "../lib/lf_ble.h"

/* v[1..] are the numeric fields of a config row (v[0] is unused) */
static inline lf_ble_config row_config(const unsigned long *v)
{
    lf_ble_config m = {0};
    m.player = (uint8_t)v[1];
    m.team = (uint8_t)v[2];
    m.friendly_fire = v[3] != 0;
    m.max_health = (uint16_t)v[4];
    m.respawn_ms = (uint16_t)v[5];
    for (int i = 0; i < 8; i++)
        m.damage_table[i] = (uint8_t)v[6 + i];
    for (int i = 0; i < LF_BLE_MODES; i++) {
        const unsigned long *p = v + 14 + 5 * i;
        m.mode[i] = (lf_ble_mode){(uint8_t)p[0], p[1] != 0, (uint16_t)p[2], (uint16_t)p[3], (uint16_t)p[4]};
    }
    return m;
}

static inline lf_ble_state row_state(const unsigned long *v)
{
    lf_ble_state m = {0};
    m.flags = (uint8_t)v[1];
    m.health = (uint16_t)v[2];
    for (int i = 0; i < LF_BLE_MODES; i++)
        m.ammo[i] = (uint16_t)v[3 + i];
    m.reloading_mask = (uint8_t)v[6];
    m.battery_pct = (uint8_t)v[7];
    return m;
}

#endif
