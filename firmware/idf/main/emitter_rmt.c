#include "emitter_rmt.h"

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "lf_emitter.h"

/* The budget in lf_emitter.h assumes at most this carrier duty. */
#define CARRIER_DUTY 0.33f
_Static_assert(33 <= LF_EMIT_CARRIER_DUTY_PERCENT, "carrier duty exceeds the emitter budget");

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;
static int s_gpio = -1;
/* 25 mark/space pairs and the trailing mark, valid until the frame is sent */
static rmt_symbol_word_t s_symbols[LF_IR_TIMINGS / 2 + 1];

esp_err_t lf_rmt_init(int gpio)
{
    s_gpio = gpio;
    rmt_tx_channel_config_t cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, /* 1 tick = 1 us */
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&cfg, &s_chan);
    if (err != ESP_OK)
        return err;
    rmt_carrier_config_t carrier = {
        .frequency_hz = LF_IR_CARRIER_HZ,
        .duty_cycle = CARRIER_DUTY,
    };
    if ((err = rmt_apply_carrier(s_chan, &carrier)) != ESP_OK)
        return err;
    rmt_copy_encoder_config_t enc = {};
    if ((err = rmt_new_copy_encoder(&enc, &s_enc)) != ESP_OK)
        return err;
    return rmt_enable(s_chan);
}

esp_err_t lf_rmt_send(const uint16_t t[LF_IR_TIMINGS])
{
    size_t n = 0;
    for (size_t i = 0; i + 1 < LF_IR_TIMINGS; i += 2, n++)
        s_symbols[n] = (rmt_symbol_word_t){.level0 = 1, .duration0 = t[i], .level1 = 0, .duration1 = t[i + 1]};
    /* trailing mark; duration 0 ends the transmission */
    s_symbols[n++] = (rmt_symbol_word_t){.level0 = 1, .duration0 = t[LF_IR_TIMINGS - 1], .level1 = 0, .duration1 = 0};
    rmt_transmit_config_t tx = {.loop_count = 0}; /* once; a looping transmit is never used */
    return rmt_transmit(s_chan, s_enc, s_symbols, n * sizeof(rmt_symbol_word_t), &tx);
}

esp_err_t lf_rmt_wait(int timeout_ms) { return rmt_tx_wait_all_done(s_chan, timeout_ms); }

void lf_rmt_kill(void)
{
    if (s_chan)
        rmt_disable(s_chan);
    if (s_gpio >= 0) {
        gpio_reset_pin(s_gpio);
        gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(s_gpio, 0);
    }
}
