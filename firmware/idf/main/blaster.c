/* The blaster: trigger -> rules (ammo, rate) -> safety gate -> IR frame. */
#include "blaster.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "emitter_rmt.h"
#include "gatt.h"
#include "lf_emitter.h"
#include "lf_link.h"
#include "lf_ir.h"
#include "lf_log.h"
#include "lf_rules.h"

static const char *TAG = "blaster";

static lf_rules s_rules;
static lf_emitter s_gate;
static SemaphoreHandle_t s_lock; /* the trigger loop and the BLE callbacks share s_rules */

static void link_config(void *c, const lf_ble_config *v)
{
    (void)c;
    if (s_rules.active)
        return; /* applies to the next match */
    lf_rules_config r = lf_rules_default_config(v->player, v->team);
    r.damage_code = v->damage_code;
    r.friendly_fire = v->friendly_fire;
    r.max_health = v->max_health;
    r.magazine = v->magazine;
    r.reload_ms = v->reload_ms;
    r.respawn_ms = v->respawn_ms;
    /* never ask for shots the safety gate would refuse after spending the ammo */
    r.fire_interval_ms = v->fire_interval_ms > s_gate.lim.min_interval_ms ? v->fire_interval_ms : s_gate.lim.min_interval_ms;
    for (int i = 0; i < 8; i++)
        r.damage_table[i] = v->damage_table[i];
    lf_rules_init(&s_rules, &r);
}

static int link_command(void *c, uint8_t cmd, uint32_t ms)
{
    (void)c;
    if (cmd == LF_CMD_START && !s_rules.active) {
        lf_rules_start(&s_rules, ms);
        return 1;
    }
    if (cmd == LF_CMD_STOP && s_rules.active) {
        lf_rules_stop(&s_rules);
        return 1;
    }
    return 0;
}

static void link_ack(void *c, uint16_t id) { (void)c; (void)id; } /* the blaster has no events */

/* battery: 100 until a divider on an ADC pin exists (not in the BOM yet) */
static void link_state(void *c, lf_ble_state *s)
{
    (void)c;
    s->flags = (uint8_t)((s_rules.active ? LF_STATE_ACTIVE : 0) | (s_rules.alive ? LF_STATE_ALIVE : 0) |
                         (s_rules.reloading ? LF_STATE_RELOADING : 0));
    s->health = s_rules.health;
    s->ammo = s_rules.ammo;
    s->battery_pct = 100;
}

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
    xSemaphoreGive(s_lock); /* the caller holds it; the BLE side may go on answering */
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
    char line[96];
    lf_log_entry e = {.kind = LF_LOG_SHOT, .t_ms = now_ms(), .sensor = -1, .frame = frame};
    if (lf_log_format(&e, line, sizeof line))
        printf("%s\n", line);
}

void blaster_run(void)
{
    ESP_ERROR_CHECK(lf_rmt_init(CONFIG_LF_PIN_IR_LED));
    gpio_reset_pin(CONFIG_LF_PIN_STATUS_LED);
    gpio_set_direction(CONFIG_LF_PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    input_pin(CONFIG_LF_PIN_TRIGGER);
    input_pin(CONFIG_LF_PIN_RELOAD);

    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&s_gate, &lim);

    lf_rules_config cfg = lf_rules_default_config(CONFIG_LF_PLAYER_ID, CONFIG_LF_TEAM);
    lf_rules_init(&s_rules, &cfg);
#if CONFIG_LF_STANDALONE
    lf_rules_start(&s_rules, now_ms());
#endif

    s_lock = xSemaphoreCreateMutex();
    static const lf_link_ops ops = {NULL, link_config, link_command, link_ack, link_state, NULL};
    gatt_start(&ops, s_lock);

    int trigger_was = 1, reload_was = 1;
    uint32_t last_auto = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int trig = gpio_get_level(CONFIG_LF_PIN_TRIGGER);
        int rel = gpio_get_level(CONFIG_LF_PIN_RELOAD);
        if (!trig && trigger_was)
            fire(&s_rules, &s_gate);
        if (!rel && reload_was)
            lf_rules_reload(&s_rules, now_ms());
        trigger_was = trig;
        reload_was = rel;
#if CONFIG_LF_BENCH_AUTOFIRE
        if (now_ms() - last_auto >= 1000) {
            last_auto = now_ms();
            fire(&s_rules, &s_gate);
        }
#else
        (void)last_auto;
#endif
        lf_rules_tick(&s_rules, now_ms());
        lf_emitter_poll(&s_gate, now_ms());
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(10)); /* 100 Hz sampling is its own debounce */
    }
}
