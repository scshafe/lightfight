/* The blaster: trigger -> rules (ammo, rate) -> safety gate -> IR frame. */
#include "blaster.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "emitter_rmt.h"
#include "lf_emitter.h"
#include "lf_ir.h"
#include "lf_rules.h"

static const char *TAG = "blaster";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void input_pin(int pin)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* switch to ground */
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

static void fault_halt(void)
{
    lf_rmt_kill();
    ESP_LOGE(TAG, "emitter fault: LED held off until reset");
    for (;;) {
        gpio_set_level(CONFIG_LF_PIN_STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(CONFIG_LF_PIN_STATUS_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void fire(lf_rules *rules, lf_emitter *gate)
{
    lf_ir_frame frame;
    lf_shot_result r = lf_rules_trigger(rules, now_ms(), &frame);
    if (r != LF_SHOT_OK) {
        ESP_LOGI(TAG, "no shot: %d", (int)r);
        return;
    }
    uint16_t t[LF_IR_TIMINGS];
    if (lf_ir_build_timings(&frame, t) != LF_IR_OK)
        return;
    lf_emit_status s = lf_emitter_request(gate, now_ms(), t);
    if (s != LF_EMIT_OK) {
        ESP_LOGW(TAG, "emitter refused: %d", (int)s);
        if (s == LF_EMIT_FAULT)
            fault_halt();
        return;
    }
    if (lf_rmt_send(t) != ESP_OK) {
        lf_rmt_kill();
        fault_halt();
    }
    if (lf_rmt_wait(200) != ESP_OK || lf_emitter_poll(gate, now_ms())) {
        fault_halt();
    }
    lf_emitter_done(gate);
    ESP_LOGI(TAG, "shot shooter=%u seq=%u ammo=%u", frame.shooter, frame.seq, (unsigned)rules->ammo);
}

void blaster_run(void)
{
    ESP_ERROR_CHECK(lf_rmt_init(CONFIG_LF_PIN_IR_LED));
    gpio_reset_pin(CONFIG_LF_PIN_STATUS_LED);
    gpio_set_direction(CONFIG_LF_PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    input_pin(CONFIG_LF_PIN_TRIGGER);
    input_pin(CONFIG_LF_PIN_RELOAD);

    lf_emitter gate;
    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&gate, &lim);

    lf_rules rules;
    lf_rules_config cfg = lf_rules_default_config(CONFIG_LF_PLAYER_ID, CONFIG_LF_TEAM);
    lf_rules_init(&rules, &cfg);
    lf_rules_start(&rules, now_ms());

    int trigger_was = 1, reload_was = 1;
    uint32_t last_auto = 0;
    for (;;) {
        int trig = gpio_get_level(CONFIG_LF_PIN_TRIGGER);
        int rel = gpio_get_level(CONFIG_LF_PIN_RELOAD);
        if (!trig && trigger_was)
            fire(&rules, &gate);
        if (!rel && reload_was)
            lf_rules_reload(&rules, now_ms());
        trigger_was = trig;
        reload_was = rel;
#if CONFIG_LF_BENCH_AUTOFIRE
        if (now_ms() - last_auto >= 1000) {
            last_auto = now_ms();
            fire(&rules, &gate);
        }
#else
        (void)last_auto;
#endif
        lf_rules_tick(&rules, now_ms());
        lf_emitter_poll(&gate, now_ms());
        vTaskDelay(pdMS_TO_TICKS(10)); /* 100 Hz sampling is its own debounce */
    }
}
