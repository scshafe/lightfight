#include "emitter_rmt.h"

#include "driver/gpio.h"
#include "lf_emitter.h"

/* The budget in lf_emitter.h assumes at most this carrier duty. */
#define CARRIER_DUTY 0.33f
_Static_assert(33 <= LF_EMIT_CARRIER_DUTY_PERCENT, "carrier duty exceeds the emitter budget");

esp_err_t lf_rmt_init(lf_rmt *e, int gpio)
{
    *e = (lf_rmt){.gpio = gpio};
    rmt_tx_channel_config_t cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, /* 1 tick = 1 us */
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&cfg, &e->chan);
    if (err != ESP_OK)
        return err;
    rmt_carrier_config_t carrier = {
        .frequency_hz = LF_IR_CARRIER_HZ,
        .duty_cycle = CARRIER_DUTY,
    };
    if ((err = rmt_apply_carrier(e->chan, &carrier)) != ESP_OK)
        return err;
    rmt_copy_encoder_config_t enc = {};
    if ((err = rmt_new_copy_encoder(&enc, &e->enc)) != ESP_OK)
        return err;
    return rmt_enable(e->chan);
}

esp_err_t lf_rmt_send(lf_rmt *e, const uint16_t t[LF_IR_TIMINGS])
{
    size_t n = 0;
    for (size_t i = 0; i + 1 < LF_IR_TIMINGS; i += 2, n++)
        e->symbols[n] = (rmt_symbol_word_t){.level0 = 1, .duration0 = t[i], .level1 = 0, .duration1 = t[i + 1]};
    /* trailing mark; duration 0 ends the transmission */
    e->symbols[n++] = (rmt_symbol_word_t){.level0 = 1, .duration0 = t[LF_IR_TIMINGS - 1], .level1 = 0, .duration1 = 0};
    rmt_transmit_config_t tx = {.loop_count = 0}; /* once; a looping transmit is never used */
    return rmt_transmit(e->chan, e->enc, e->symbols, n * sizeof(rmt_symbol_word_t), &tx);
}

esp_err_t lf_rmt_wait(lf_rmt *e, int timeout_ms) { return rmt_tx_wait_all_done(e->chan, timeout_ms); }

void lf_rmt_kill(lf_rmt *e)
{
    if (e->chan)
        rmt_disable(e->chan);
    if (e->gpio >= 0) {
        gpio_reset_pin(e->gpio);
        gpio_set_direction(e->gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(e->gpio, 0);
    }
}
