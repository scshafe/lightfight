#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "lf_battery.h"

static const char *TAG = "battery";

static lf_battery s_batt;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t battery_percent(void)
{
    portENTER_CRITICAL(&s_mux);
    uint8_t p = lf_battery_percent(&s_batt);
    portEXIT_CRITICAL(&s_mux);
    return p;
}

uint32_t battery_mv(void)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t mv = s_batt.primed ? lf_battery_mv(&s_batt) : 0;
    portEXIT_CRITICAL(&s_mux);
    return mv;
}

static void task(void *arg)
{
    (void)arg;
    adc_unit_t unit;
    adc_channel_t chan;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(CONFIG_LF_PIN_BATTERY, &unit, &chan));
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &adc));
    adc_oneshot_chan_cfg_t ccfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, chan, &ccfg));
    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t kcfg = {
        .unit_id = unit, .chan = chan, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&kcfg, &cali));

    for (;;) {
        int sum = 0, n = 0;
        for (int i = 0; i < 16; i++) {
            int mv;
            int raw;
            if (adc_oneshot_read(adc, chan, &raw) == ESP_OK && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
                sum += mv;
                n++;
            }
        }
        if (n) {
            uint32_t cell = lf_battery_cell_mv((uint32_t)(sum / n), CONFIG_LF_BATTERY_CAL_PERMILLE);
            portENTER_CRITICAL(&s_mux);
            lf_battery_sample(&s_batt, cell);
            portEXIT_CRITICAL(&s_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void battery_start(void)
{
    lf_battery_init(&s_batt);
    xTaskCreate(task, "battery", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "sensing on GPIO%d", CONFIG_LF_PIN_BATTERY);
}
