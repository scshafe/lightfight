#include "lf_ble.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

size_t lf_ble_encode_config(const lf_ble_config *m, uint8_t out[LF_BLE_CONFIG_LEN])
{
    if (m->player > 63 || m->team > 7 || m->damage_code > 7)
        return 0;
    out[0] = LF_BLE_VERSION;
    out[1] = m->player;
    out[2] = m->team;
    out[3] = m->damage_code;
    out[4] = m->friendly_fire ? 1 : 0;
    put16(out + 5, m->max_health);
    put16(out + 7, m->magazine);
    put16(out + 9, m->reload_ms);
    put16(out + 11, m->respawn_ms);
    put16(out + 13, m->fire_interval_ms);
    for (int i = 0; i < 8; i++)
        out[15 + i] = m->damage_table[i];
    return LF_BLE_CONFIG_LEN;
}

bool lf_ble_decode_config(const uint8_t *b, size_t n, lf_ble_config *m)
{
    if (n != LF_BLE_CONFIG_LEN || b[0] != LF_BLE_VERSION || b[1] > 63 || b[2] > 7 ||
        b[3] > 7 || b[4] > 1)
        return false;
    m->player = b[1];
    m->team = b[2];
    m->damage_code = b[3];
    m->friendly_fire = b[4] == 1;
    m->max_health = get16(b + 5);
    m->magazine = get16(b + 7);
    m->reload_ms = get16(b + 9);
    m->respawn_ms = get16(b + 11);
    m->fire_interval_ms = get16(b + 13);
    for (int i = 0; i < 8; i++)
        m->damage_table[i] = b[15 + i];
    return true;
}

size_t lf_ble_encode_state(const lf_ble_state *m, uint8_t out[LF_BLE_STATE_LEN])
{
    if (m->flags > 7 || m->battery_pct > 100)
        return 0;
    out[0] = LF_BLE_VERSION;
    out[1] = m->flags;
    put16(out + 2, m->health);
    put16(out + 4, m->ammo);
    out[6] = m->battery_pct;
    return LF_BLE_STATE_LEN;
}

bool lf_ble_decode_state(const uint8_t *b, size_t n, lf_ble_state *m)
{
    if (n != LF_BLE_STATE_LEN || b[0] != LF_BLE_VERSION || b[1] > 7 || b[6] > 100)
        return false;
    m->flags = b[1];
    m->health = get16(b + 2);
    m->ammo = get16(b + 4);
    m->battery_pct = b[6];
    return true;
}

size_t lf_ble_encode_event(const lf_ble_event *m, uint8_t out[LF_BLE_EVENT_LEN])
{
    if ((m->kind != LF_EVENT_HIT && m->kind != LF_EVENT_DOWN) || m->shooter > 63 ||
        m->seq > 15)
        return 0;
    out[0] = LF_BLE_VERSION;
    out[1] = m->kind;
    put16(out + 2, m->event_id);
    out[4] = m->shooter;
    out[5] = m->seq;
    out[6] = m->damage;
    put16(out + 7, m->health_after);
    put32(out + 9, m->t_ms);
    return LF_BLE_EVENT_LEN;
}

bool lf_ble_decode_event(const uint8_t *b, size_t n, lf_ble_event *m)
{
    if (n != LF_BLE_EVENT_LEN || b[0] != LF_BLE_VERSION ||
        (b[1] != LF_EVENT_HIT && b[1] != LF_EVENT_DOWN) || b[4] > 63 || b[5] > 15)
        return false;
    m->kind = b[1];
    m->event_id = get16(b + 2);
    m->shooter = b[4];
    m->seq = b[5];
    m->damage = b[6];
    m->health_after = get16(b + 7);
    m->t_ms = get32(b + 9);
    return true;
}

size_t lf_ble_encode_ack(uint16_t event_id, uint8_t out[LF_BLE_ACK_LEN])
{
    out[0] = LF_BLE_VERSION;
    put16(out + 1, event_id);
    return LF_BLE_ACK_LEN;
}

bool lf_ble_decode_ack(const uint8_t *b, size_t n, uint16_t *event_id)
{
    if (n != LF_BLE_ACK_LEN || b[0] != LF_BLE_VERSION)
        return false;
    *event_id = get16(b + 1);
    return true;
}

size_t lf_ble_encode_command(uint8_t cmd, uint8_t out[LF_BLE_COMMAND_LEN])
{
    if (cmd < LF_CMD_START || cmd > LF_CMD_PING)
        return 0;
    out[0] = LF_BLE_VERSION;
    out[1] = cmd;
    return LF_BLE_COMMAND_LEN;
}

bool lf_ble_decode_command(const uint8_t *b, size_t n, uint8_t *cmd)
{
    if (n != LF_BLE_COMMAND_LEN || b[0] != LF_BLE_VERSION || b[1] < LF_CMD_START ||
        b[1] > LF_CMD_PING)
        return false;
    *cmd = b[1];
    return true;
}

void lf_evq_init(lf_evq *q)
{
    q->head = 0;
    q->count = 0;
    q->next_id = 1;
}

uint16_t lf_evq_push(lf_evq *q, lf_ble_event *e)
{
    e->event_id = q->next_id;
    q->next_id = (uint16_t)(q->next_id + 1);
    if (q->next_id == 0)
        q->next_id = 1;
    if (q->count == LF_EVQ_SLOTS) {
        q->head = (q->head + 1) % LF_EVQ_SLOTS;
        q->count--;
    }
    q->ev[(q->head + q->count) % LF_EVQ_SLOTS] = *e;
    q->count++;
    return e->event_id;
}

void lf_evq_ack(lf_evq *q, uint16_t event_id)
{
    /* ids are within LF_EVQ_SLOTS of each other, so a signed 16-bit difference
     * orders them across the wrap */
    while (q->count > 0 &&
           (int16_t)(event_id - q->ev[q->head].event_id) >= 0) {
        q->head = (q->head + 1) % LF_EVQ_SLOTS;
        q->count--;
    }
}

const lf_ble_event *lf_evq_pending(const lf_evq *q, unsigned i)
{
    return i < q->count ? &q->ev[(q->head + i) % LF_EVQ_SLOTS] : NULL;
}
