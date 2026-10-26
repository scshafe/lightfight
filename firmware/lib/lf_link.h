/* The gear's side of the phone link (docs/ble-protocol.md), without the BLE
 * stack: what a write to each characteristic does, and which notification to
 * send next (queued events in order, replayed after a reconnection, then the
 * state). The ESP-IDF glue (idf/main/gatt.c) only moves bytes between NimBLE
 * and this, so the protocol logic is tested on the host. */
#ifndef LF_LINK_H
#define LF_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "lf_ble.h"

/* The service and characteristic UUIDs 4c46000X-7f1a-4b6e-9c3d-2a5e8b1d0f47,
 * as the 16 bytes a BLE stack wants (least significant first). X: 0 service,
 * 1 config, 2 command, 3 state, 4 events, 5 ack. */
#define LF_UUID128_LE(x) 0x47, 0x0f, 0x1d, 0x8b, 0x5e, 0x2a, 0x3d, 0x9c, 0x6e, 0x4b, 0x1a, 0x7f, (x), 0x00, 0x46, 0x4c

typedef struct {
    void *ctx;
    void (*apply_config)(void *ctx, const lf_ble_config *c);
    int (*command)(void *ctx, uint8_t cmd, uint32_t now_ms);
    void (*ack)(void *ctx, uint16_t event_id);
    void (*get_state)(void *ctx, lf_ble_state *s);
    /* i-th unacknowledged event, oldest first, or NULL; NULL function = the
     * device has no events (the blaster) */
    const lf_ble_event *(*pending)(void *ctx, unsigned i);
} lf_link_ops;

typedef enum { LF_CHR_CONFIG = 1, LF_CHR_COMMAND, LF_CHR_ACK } lf_chr;
typedef enum { LF_NOTIFY_STATE = 1, LF_NOTIFY_EVENTS = 2 } lf_notify_mask;
typedef enum { LF_LINK_OK = 0, LF_LINK_BAD_LENGTH, LF_LINK_BAD_VALUE } lf_link_status;

#define LF_LINK_STATE_PERIOD_MS 1000u
#define LF_LINK_NOTIFY_MAX LF_BLE_CONFIG_LEN /* the longest message we send is an event (13) */

typedef struct {
    lf_link_ops ops;
    int connected;
    unsigned replay_next;  /* index into pending() of the next event to send */
    int state_valid;
    lf_ble_state last_state;
    uint32_t last_state_ms;
    /* what lf_link_next last returned, so a failed send can be undone */
    int last_was_event;
    int last_was_state;
    int last_state_was_valid;
    lf_ble_state last_state_before;
    uint32_t last_state_ms_before;
} lf_link;

typedef struct {
    lf_notify_mask which;
    uint8_t data[LF_LINK_NOTIFY_MAX];
    size_t len;
} lf_notification;

void lf_link_init(lf_link *l, const lf_link_ops *ops);

/* A phone connected or went away. Everything unacknowledged is sent again to
 * the next phone, in order. */
void lf_link_set_connected(lf_link *l, int connected);
/* The phone (re)subscribed to a characteristic: start that stream over. */
void lf_link_resubscribed(lf_link *l, lf_notify_mask which);

/* A write to a characteristic. A bad write changes nothing. */
lf_link_status lf_link_write(lf_link *l, lf_chr chr, const uint8_t *data, size_t len, uint32_t now_ms);

/* The next notification to send, among the subscribed streams, or 0 for none.
 * Events first, in order; the state when it changed or LF_LINK_STATE_PERIOD_MS
 * passed. */
int lf_link_next(lf_link *l, uint32_t now_ms, unsigned subscribed, lf_notification *out);
/* The stack could not send what lf_link_next just returned: offer it again. */
void lf_link_unsend(lf_link *l);

/* The value a read of the state characteristic returns. */
size_t lf_link_read_state(lf_link *l, uint8_t out[LF_BLE_STATE_LEN]);

#endif
