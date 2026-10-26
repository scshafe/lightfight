/* The vest hub: five IR receivers -> edges -> lf_hub (portable) -> haptics.
 * GPIO interrupts timestamp every edge into a ring; the task below feeds the
 * portable core. ESP32-C3 has only two RMT receive channels, so the five
 * sensors use GPIO interrupts instead; the receive tolerance (+-140 us per
 * mark) covers the interrupt latency. */
#include "hub.h"

#include "sdkconfig.h"

#if CONFIG_LF_ROLE_HUB

#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lf_hub.h"
#include "lf_log.h"

static const char *TAG = "hub";

#define RING 512u
typedef struct {
    uint32_t t_us;
    uint8_t sensor, level;
} edge_t;

static const int s_pins[LF_HUB_SENSORS] = {
    CONFIG_LF_PIN_SENSOR0, CONFIG_LF_PIN_SENSOR1, CONFIG_LF_PIN_SENSOR2,
    CONFIG_LF_PIN_SENSOR3, CONFIG_LF_PIN_SENSOR4,
};
static edge_t s_ring[RING];
static volatile uint32_t s_head, s_tail, s_dropped;
static lf_hub s_hub;

static void IRAM_ATTR on_edge(void *arg)
{
    int sensor = (int)(intptr_t)arg;
    uint32_t h = s_head, next = (h + 1) % RING;
    if (next == s_tail) {
        s_dropped++;
        return;
    }
    s_ring[h] = (edge_t){(uint32_t)esp_timer_get_time(), (uint8_t)sensor, (uint8_t)gpio_get_level(s_pins[sensor])};
    s_head = next;
}

static void log_line(void *ctx, const lf_log_entry *e)
{
    (void)ctx;
    char line[160];
    if (lf_log_format(e, line, sizeof line))
        printf("%s\n", line);
}

static uint32_t now_us(void) { return (uint32_t)esp_timer_get_time(); }

static void output_pin(int pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

void hub_run(void)
{
    output_pin(CONFIG_LF_PIN_HAPTIC);
    output_pin(CONFIG_LF_PIN_BUZZER);
    output_pin(CONFIG_LF_PIN_STATUS_LED);

    lf_rules_config cfg = lf_rules_default_config(CONFIG_LF_PLAYER_ID, CONFIG_LF_TEAM);
    lf_hub_init(&s_hub, &cfg, log_line, NULL);
#if CONFIG_LF_STANDALONE
    lf_hub_command(&s_hub, LF_CMD_START, now_us() / 1000);
#endif

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < LF_HUB_SENSORS; i++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, /* receiver outputs idle high */
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_pins[i], on_edge, (void *)(intptr_t)i));
    }
    ESP_LOGI(TAG, "hub up: player %d team %d", CONFIG_LF_PLAYER_ID, CONFIG_LF_TEAM);

    uint32_t haptic_off_ms = 0, last_counters_ms = 0, last_dropped = 0;
    int haptic_on = 0;
    for (;;) {
        while (s_tail != s_head) {
            edge_t e = s_ring[s_tail];
            s_tail = (s_tail + 1) % RING;
            lf_hub_edge(&s_hub, e.sensor, e.level, e.t_us);
        }
        lf_hub_poll_result r = lf_hub_poll(&s_hub, now_us());
        uint32_t ms = now_us() / 1000;
        if (r.hits) {
            haptic_on = 1;
            haptic_off_ms = ms + (r.downs ? 600 : 150);
            gpio_set_level(CONFIG_LF_PIN_HAPTIC, 1);
            gpio_set_level(CONFIG_LF_PIN_BUZZER, r.downs ? 1 : 0);
        }
        if (haptic_on && (int32_t)(ms - haptic_off_ms) >= 0) {
            haptic_on = 0;
            gpio_set_level(CONFIG_LF_PIN_HAPTIC, 0);
            gpio_set_level(CONFIG_LF_PIN_BUZZER, 0);
        }
        gpio_set_level(CONFIG_LF_PIN_STATUS_LED, s_hub.rules.alive ? 1 : 0);
        if (ms - last_counters_ms >= 10000) {
            last_counters_ms = ms;
            lf_hub_log_counters(&s_hub, ms);
            if (s_dropped != last_dropped) {
                ESP_LOGW(TAG, "edge ring overflowed %u times", (unsigned)(s_dropped - last_dropped));
                last_dropped = s_dropped;
            }
        }
        vTaskDelay(1);
    }
}

#endif /* CONFIG_LF_ROLE_HUB */
