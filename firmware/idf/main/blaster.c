/* The blaster: trigger -> rules (ammo, rate) -> safety gate -> IR frame. */
#include "blaster.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "emitter_rmt.h"
#include "battery.h"
#include "gatt.h"
#include "lf_conv.h"
#include "lf_emitter.h"
#include "lf_link.h"
#include "lf_ir.h"
#include "lf_log.h"
#include "lf_rules.h"

static const char *TAG = "blaster";

static lf_rules s_rules;
static lf_emitter s_gate;
static lf_rmt s_led[2]; /* 0 narrow beam (light, heavy), 1 wide shotgun LED */
static SemaphoreHandle_t s_lock; /* the trigger loop and the BLE callbacks share s_rules */

static void link_config(void *c, const lf_ble_config *v)
{
    (void)c;
    if (s_rules.active)
        return; /* applies to the next match */
    /* the gap between any two shots is the gate's minimum: never spend a round
     * on a shot the safety gate would then refuse */
    lf_rules_config r = lf_conv_rules_config(v, s_gate.lim.min_interval_ms);
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
    if (cmd == LF_CMD_DISABLE && s_rules.active) { /* the player is down (relayed by the phone) */
        s_rules.disabled = true;
        return 1;
    }
    if (cmd == LF_CMD_ENABLE && s_rules.active) {
        s_rules.disabled = false;
        return 1;
    }
    return 0;
}

static void link_ack(void *c, uint16_t id) { (void)c; (void)id; } /* the blaster has no events */

static void link_state(void *c, lf_ble_state *s)
{
    (void)c;
    lf_conv_state(&s_rules, battery_percent(), s);
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
    lf_rmt_kill(&s_led[0]);
    lf_rmt_kill(&s_led[1]);
    xSemaphoreGive(s_lock); /* the caller holds it; the BLE side may go on answering */
    ESP_LOGE(TAG, "emitter fault: LED held off until reset");
    for (;;) {
        gpio_set_level(CONFIG_LF_PIN_STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(CONFIG_LF_PIN_STATUS_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Sounds: a mode's table of (frequency Hz, duration ms) steps, 0 Hz = rest.
 * Light and shotgun are silent (owner decision 2026-10-26); the heavy mode is
 * the one others can hear. */
typedef struct { uint16_t hz, ms; } tone_t;
static const tone_t k_heavy_sound[] = {{1800, 40}, {900, 60}, {450, 120}, {0, 0}};
static const tone_t *const k_sound[LF_MODES] = {NULL, k_heavy_sound, NULL};

static TaskHandle_t s_sound_task;

static void sound_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t mode;
        xTaskNotifyWait(0, 0, &mode, portMAX_DELAY);
        const tone_t *t = k_sound[mode % LF_MODES];
        for (; t && t->ms; t++) {
            if (t->hz) {
                ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, t->hz);
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            } else {
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(t->ms));
        }
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    }
}

static void sound_start(void)
{
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                                 .timer_num = LEDC_TIMER_0, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {.gpio_num = CONFIG_LF_PIN_BUZZER, .speed_mode = LEDC_LOW_SPEED_MODE,
                                .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    xTaskCreate(sound_task, "sound", 2048, NULL, 2, &s_sound_task);
}

static void fire(lf_rules *rules, lf_emitter *gate, uint8_t mode)
{
    lf_ir_frame frame;
    lf_shot_result r = lf_rules_trigger(rules, mode, now_ms(), &frame);
    if (r != LF_SHOT_OK) {
        ESP_LOGI(TAG, "no shot (mode %d): %d", (int)mode, (int)r);
        return;
    }
    lf_rmt *led = &s_led[mode == LF_MODE_SHOTGUN ? 1 : 0];
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
    if (lf_rmt_send(led, t) != ESP_OK) {
        fault_halt();
    }
    if (k_sound[mode])
        xTaskNotify(s_sound_task, mode, eSetValueWithOverwrite);
    if (lf_rmt_wait(led, 200) != ESP_OK || lf_emitter_poll(gate, now_ms())) {
        fault_halt();
    }
    lf_emitter_done(gate);
    char line[96];
    lf_log_entry e = {.kind = LF_LOG_SHOT, .t_ms = now_ms(), .sensor = -1, .frame = frame};
    if (lf_log_format(&e, line, sizeof line))
        printf("%s [mode %d]\n", line, (int)mode);
}

#define RELOAD_HOLD_MS 800 /* a press held this long reloads every mode */

void blaster_run(void)
{
    ESP_ERROR_CHECK(lf_rmt_init(&s_led[0], CONFIG_LF_PIN_IR_LED));
    ESP_ERROR_CHECK(lf_rmt_init(&s_led[1], CONFIG_LF_PIN_IR_WIDE));
    gpio_reset_pin(CONFIG_LF_PIN_STATUS_LED);
    gpio_set_direction(CONFIG_LF_PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    const int trigger_pin[LF_MODES] = {CONFIG_LF_PIN_TRIGGER, CONFIG_LF_PIN_TRIGGER_B, CONFIG_LF_PIN_TRIGGER_C};
    for (int m = 0; m < LF_MODES; m++)
        input_pin(trigger_pin[m]);
    input_pin(CONFIG_LF_PIN_RELOAD);
    sound_start();

    lf_emitter_limits lim = lf_emitter_default_limits();
    lf_emitter_init(&s_gate, &lim);

    lf_rules_config cfg = lf_rules_default_config(CONFIG_LF_PLAYER_ID, CONFIG_LF_TEAM);
    cfg.gap_ms = lim.min_interval_ms;
#if CONFIG_LF_BENCH_AUTOFIRE
    for (int m = 0; m < LF_MODES; m++) { /* never run dry or wait on a cooldown during a capture */
        cfg.mode[m].magazine = 60000;
        cfg.mode[m].fire_interval_ms = 0;
    }
#endif
    lf_rules_init(&s_rules, &cfg);
#if CONFIG_LF_STANDALONE
    lf_rules_start(&s_rules, now_ms());
#endif

    s_lock = xSemaphoreCreateMutex();
    static const lf_link_ops ops = {NULL, link_config, link_command, link_ack, link_state, NULL};
    battery_start();
    gatt_start(&ops, s_lock);

    int trigger_was[LF_MODES] = {1, 1, 1}, reload_was = 1;
    uint32_t reload_down_ms = 0;
    bool reload_all_done = false;
    uint32_t last_auto = 0;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int m = 0; m < LF_MODES; m++) {
            int level = gpio_get_level(trigger_pin[m]);
            if (!level && trigger_was[m])
                fire(&s_rules, &s_gate, (uint8_t)m);
            trigger_was[m] = level;
        }
        int rel = gpio_get_level(CONFIG_LF_PIN_RELOAD);
        if (!rel && reload_was) {
            reload_down_ms = now_ms();
            reload_all_done = false;
        }
        if (!rel && !reload_all_done && now_ms() - reload_down_ms >= RELOAD_HOLD_MS) {
            lf_rules_reload_all(&s_rules, now_ms()); /* held: every mode */
            reload_all_done = true;
        }
        if (rel && !reload_was && !reload_all_done)
            lf_rules_reload(&s_rules, now_ms()); /* short press: the mode fired last */
        reload_was = rel;
#if CONFIG_LF_BENCH_AUTOFIRE
        if (now_ms() - last_auto >= 1000) {
            last_auto = now_ms();
            fire(&s_rules, &s_gate, CONFIG_LF_BENCH_AUTOFIRE_MODE);
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
