/* lightfight BLE messages (docs/ble-protocol.md): fixed little-endian layouts,
 * a version byte first. Decoders reject a wrong length, version or range. */
#ifndef LF_BLE_H
#define LF_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LF_BLE_VERSION 1

#define LF_BLE_CONFIG_LEN 23
#define LF_BLE_STATE_LEN 7
#define LF_BLE_EVENT_LEN 13
#define LF_BLE_ACK_LEN 3
#define LF_BLE_COMMAND_LEN 2

enum { LF_STATE_ACTIVE = 1, LF_STATE_ALIVE = 2, LF_STATE_RELOADING = 4 };
enum { LF_EVENT_HIT = 1, LF_EVENT_DOWN = 2 };
enum { LF_CMD_START = 1, LF_CMD_STOP = 2, LF_CMD_RESPAWN = 3, LF_CMD_PING = 4 };

typedef struct {
    uint8_t player, team, damage_code;
    bool friendly_fire;
    uint16_t max_health, magazine, reload_ms, respawn_ms, fire_interval_ms;
    uint8_t damage_table[8];
} lf_ble_config;

typedef struct {
    uint8_t flags;
    uint16_t health, ammo;
    uint8_t battery_pct;
} lf_ble_state;

typedef struct {
    uint8_t kind;
    uint16_t event_id;
    uint8_t shooter, seq, damage;
    uint16_t health_after;
    uint32_t t_ms;
} lf_ble_event;

/* Encoders write the exact length and return it, or 0 for an out-of-range field. */
size_t lf_ble_encode_config(const lf_ble_config *m, uint8_t out[LF_BLE_CONFIG_LEN]);
size_t lf_ble_encode_state(const lf_ble_state *m, uint8_t out[LF_BLE_STATE_LEN]);
size_t lf_ble_encode_event(const lf_ble_event *m, uint8_t out[LF_BLE_EVENT_LEN]);
size_t lf_ble_encode_ack(uint16_t event_id, uint8_t out[LF_BLE_ACK_LEN]);
size_t lf_ble_encode_command(uint8_t cmd, uint8_t out[LF_BLE_COMMAND_LEN]);

/* Decoders return true and fill *m on a valid message. */
bool lf_ble_decode_config(const uint8_t *b, size_t n, lf_ble_config *m);
bool lf_ble_decode_state(const uint8_t *b, size_t n, lf_ble_state *m);
bool lf_ble_decode_event(const uint8_t *b, size_t n, lf_ble_event *m);
bool lf_ble_decode_ack(const uint8_t *b, size_t n, uint16_t *event_id);
bool lf_ble_decode_command(const uint8_t *b, size_t n, uint8_t *cmd);

/* The gear's event queue: events get ids 1, 2, ... (u16, wrapping, 0 skipped),
 * stay queued until the phone acknowledges them (cumulative ack) and are
 * replayed in order after a reconnection. When full, the oldest is dropped. */
#define LF_EVQ_SLOTS 32

typedef struct {
    lf_ble_event ev[LF_EVQ_SLOTS];
    unsigned head;      /* index of the oldest queued event */
    unsigned count;
    uint16_t next_id;
} lf_evq;

void lf_evq_init(lf_evq *q);
/* Assigns the id (and returns it) and stores the event; fills e->event_id. */
uint16_t lf_evq_push(lf_evq *q, lf_ble_event *e);
/* Drops every queued event up to and including event_id (wrap aware). */
void lf_evq_ack(lf_evq *q, uint16_t event_id);
/* The i-th pending event (0 = oldest), or NULL. Replay = iterate from 0. */
const lf_ble_event *lf_evq_pending(const lf_evq *q, unsigned i);

#endif
