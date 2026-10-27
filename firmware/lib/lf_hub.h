/* The vest hub's core: N receiver channels -> frames -> rules -> events for
 * the phone. Everything the hub decides lives here, off the hardware, so it is
 * tested on the host; the ESP-IDF glue only timestamps edges and drives the
 * haptics. */
#ifndef LF_HUB_H
#define LF_HUB_H

#include <stdint.h>

#include "lf_ble.h"
#include "lf_haptic.h"
#include "lf_log.h"
#include "lf_rules.h"
#include "lf_rx.h"

#define LF_HUB_SENSORS 5

typedef void (*lf_hub_log_fn)(void *ctx, const lf_log_entry *e);

/* A counted hit waits this long for the other sensors that saw the same shot,
 * so a headshot that a chest sensor decoded first still buzzes the headband. */
#define LF_HUB_ZONE_SETTLE_MS 15

/* sensors 0-2 are on the vest (chest, shoulders), 3-4 on the headband */
uint8_t lf_hub_sensor_zone(int sensor);

typedef struct {
    bool valid;
    uint8_t zones;                  /* LF_ZONE_* bits of the sensors that read the shot */
    uint8_t shooter, seq;
    lf_cue cue;
    uint32_t due_ms;
} lf_hub_pending_cue;

typedef struct {
    lf_rules rules;
    lf_haptic haptic;
    lf_hub_pending_cue pending;
    lf_evq events;
    lf_rx_chan chan[LF_HUB_SENSORS];
    uint32_t frames_read[LF_HUB_SENSORS]; /* valid frames per sensor, counted or not */
    lf_hub_log_fn log;
    void *log_ctx;
} lf_hub;

typedef struct {
    unsigned hits;  /* hits counted by this poll */
    unsigned downs; /* of which put the player down */
} lf_hub_poll_result;

void lf_hub_init(lf_hub *h, const lf_rules_config *cfg, lf_hub_log_fn log, void *ctx);

/* An edge on one sensor (level after the edge, 0 = mark). */
void lf_hub_edge(lf_hub *h, int sensor, int level, uint32_t t_us);

/* Call every millisecond or so: finishes bursts, decodes, applies rules and
 * queues events for the phone. */
lf_hub_poll_result lf_hub_poll(lf_hub *h, uint32_t now_us);

/* The haptic outputs (LF_OUT_* bits) at now_ms. See lf_haptic.h and
 * docs/design.md, "Haptics". */
uint8_t lf_hub_outputs(lf_hub *h, uint32_t now_ms);

/* BLE side (docs/ble-protocol.md). */
void lf_hub_apply_config(lf_hub *h, const lf_ble_config *c);
int lf_hub_command(lf_hub *h, uint8_t cmd, uint32_t now_ms); /* 1 if it did something */
void lf_hub_state(const lf_hub *h, uint8_t battery_pct, lf_ble_state *out);
void lf_hub_ack(lf_hub *h, uint16_t event_id);

/* Emit the receivers' counters to the log (call now and then). */
void lf_hub_log_counters(lf_hub *h, uint32_t now_ms);

#endif
