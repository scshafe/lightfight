/* The IR LED driver: the RMT peripheral keys a 38 kHz carrier. */
#ifndef EMITTER_RMT_H
#define EMITTER_RMT_H

#include "esp_err.h"
#include "lf_ir.h"

esp_err_t lf_rmt_init(int gpio);
/* Starts one frame; returns at once. Never loops. */
esp_err_t lf_rmt_send(const uint16_t t[LF_IR_TIMINGS]);
/* Waits for the end of the frame; ESP_ERR_TIMEOUT if it does not end. */
esp_err_t lf_rmt_wait(int timeout_ms);
/* Stops the channel and holds the pin low. Used on a fault. */
void lf_rmt_kill(void);

#endif
