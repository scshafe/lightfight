#include <string.h>

#include "lf_link.h"

static unsigned pending_count(const lf_link *l)
{
    if (!l->ops.pending)
        return 0;
    unsigned n = 0;
    while (l->ops.pending(l->ops.ctx, n))
        n++;
    return n;
}

static int same_state(const lf_ble_state *a, const lf_ble_state *b)
{
    return a->flags == b->flags && a->health == b->health && !memcmp(a->ammo, b->ammo, sizeof a->ammo) &&
           a->reloading_mask == b->reloading_mask && a->battery_pct == b->battery_pct;
}

void lf_link_init(lf_link *l, const lf_link_ops *ops)
{
    memset(l, 0, sizeof *l);
    l->ops = *ops;
}

void lf_link_set_connected(lf_link *l, int connected)
{
    l->connected = connected;
    l->replay_next = 0;
    l->state_valid = 0;
}

void lf_link_resubscribed(lf_link *l, lf_notify_mask which)
{
    if (which & LF_NOTIFY_EVENTS)
        l->replay_next = 0;
    if (which & LF_NOTIFY_STATE)
        l->state_valid = 0;
}

lf_link_status lf_link_write(lf_link *l, lf_chr chr, const uint8_t *data, size_t len, uint32_t now_ms)
{
    switch (chr) {
    case LF_CHR_CONFIG: {
        if (len != LF_BLE_CONFIG_LEN)
            return LF_LINK_BAD_LENGTH;
        lf_ble_config c;
        if (!lf_ble_decode_config(data, len, &c))
            return LF_LINK_BAD_VALUE;
        l->ops.apply_config(l->ops.ctx, &c);
        return LF_LINK_OK;
    }
    case LF_CHR_COMMAND: {
        if (len != LF_BLE_COMMAND_LEN)
            return LF_LINK_BAD_LENGTH;
        uint8_t cmd;
        if (!lf_ble_decode_command(data, len, &cmd))
            return LF_LINK_BAD_VALUE;
        l->ops.command(l->ops.ctx, cmd, now_ms);
        return LF_LINK_OK;
    }
    case LF_CHR_ACK: {
        if (len != LF_BLE_ACK_LEN)
            return LF_LINK_BAD_LENGTH;
        uint16_t id;
        if (!lf_ble_decode_ack(data, len, &id))
            return LF_LINK_BAD_VALUE;
        unsigned before = pending_count(l);
        l->ops.ack(l->ops.ctx, id);
        unsigned dropped = before - pending_count(l);
        l->replay_next = l->replay_next > dropped ? l->replay_next - dropped : 0;
        return LF_LINK_OK;
    }
    }
    return LF_LINK_BAD_VALUE;
}

int lf_link_next(lf_link *l, uint32_t now_ms, unsigned subscribed, lf_notification *out)
{
    l->last_was_event = l->last_was_state = 0;
    if (!l->connected)
        return 0;
    if ((subscribed & LF_NOTIFY_EVENTS) && l->ops.pending) {
        unsigned n = pending_count(l);
        if (l->replay_next > n) /* the oldest were dropped from a full queue */
            l->replay_next = n;
        const lf_ble_event *e = l->ops.pending(l->ops.ctx, l->replay_next);
        if (e) {
            out->which = LF_NOTIFY_EVENTS;
            out->len = lf_ble_encode_event(e, out->data);
            if (out->len) {
                l->replay_next++;
                l->last_was_event = 1;
                return 1;
            }
            l->replay_next++; /* an event the codec refuses can never be sent: skip it */
        }
    }
    if (subscribed & LF_NOTIFY_STATE) {
        lf_ble_state s;
        l->ops.get_state(l->ops.ctx, &s);
        int due = !l->state_valid || !same_state(&s, &l->last_state) ||
                  (int32_t)(now_ms - (l->last_state_ms + LF_LINK_STATE_PERIOD_MS)) >= 0;
        if (due) {
            out->which = LF_NOTIFY_STATE;
            out->len = lf_ble_encode_state(&s, out->data);
            if (!out->len)
                return 0;
            l->last_state_before = l->last_state;
            l->last_state_ms_before = l->last_state_ms;
            l->last_state_was_valid = l->state_valid;
            l->last_state = s;
            l->last_state_ms = now_ms;
            l->state_valid = 1;
            l->last_was_state = 1;
            return 1;
        }
    }
    return 0;
}

void lf_link_unsend(lf_link *l)
{
    if (l->last_was_event && l->replay_next > 0)
        l->replay_next--;
    if (l->last_was_state) {
        l->last_state = l->last_state_before;
        l->last_state_ms = l->last_state_ms_before;
        l->state_valid = l->last_state_was_valid;
    }
    l->last_was_event = l->last_was_state = 0;
}

size_t lf_link_read_state(lf_link *l, uint8_t out[LF_BLE_STATE_LEN])
{
    lf_ble_state s;
    l->ops.get_state(l->ops.ctx, &s);
    return lf_ble_encode_state(&s, out);
}
