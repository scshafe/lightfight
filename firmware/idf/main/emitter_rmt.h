/* The IR LED drivers: the RMT peripheral keys a 38 kHz carrier. One instance
 * per LED (the narrow beam and the wide shotgun LED); the ESP32-C3 has two TX
 * channels, which is exactly enough. Only one LED sends at a time (the
 * emitter gate's busy lock). */
#ifndef EMITTER_RMT_H
#define EMITTER_RMT_H

#include "esp_err.h"
#include "lf_ir.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

typedef struct {
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t enc;
    int gpio;
    /* 25 mark/space pairs and the trailing mark, valid until the frame is sent */
    rmt_symbol_word_t symbols[LF_IR_TIMINGS / 2 + 1];
} lf_rmt;

esp_err_t lf_rmt_init(lf_rmt *e, int gpio);
/* Starts one frame; returns at once. Never loops. */
esp_err_t lf_rmt_send(lf_rmt *e, const uint16_t t[LF_IR_TIMINGS]);
/* Waits for the end of the frame; ESP_ERR_TIMEOUT if it does not end. */
esp_err_t lf_rmt_wait(lf_rmt *e, int timeout_ms);
/* Stops the channel and holds the pin low. Used on a fault. */
void lf_rmt_kill(lf_rmt *e);

#endif
