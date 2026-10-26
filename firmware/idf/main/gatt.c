#include "gatt.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

static const char *TAG = "gatt";

#define PAIR_OPEN_MS 60000u
#define PAIR_HOLD_MS 3000u
#define CLEAR_HOLD_MS 10000u

static lf_link s_link;
static SemaphoreHandle_t s_lock;
static char s_name[8];
static uint8_t s_own_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_state_handle, s_events_handle;
static unsigned s_subscribed;
static volatile int64_t s_pair_until_us;

static const ble_uuid128_t u_service = BLE_UUID128_INIT(LF_UUID128_LE(0));
static const ble_uuid128_t u_config = BLE_UUID128_INIT(LF_UUID128_LE(1));
static const ble_uuid128_t u_command = BLE_UUID128_INIT(LF_UUID128_LE(2));
static const ble_uuid128_t u_state = BLE_UUID128_INIT(LF_UUID128_LE(3));
static const ble_uuid128_t u_events = BLE_UUID128_INIT(LF_UUID128_LE(4));
static const ble_uuid128_t u_ack = BLE_UUID128_INIT(LF_UUID128_LE(5));

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static int pairing_open(void) { return esp_timer_get_time() < s_pair_until_us; }

void gatt_open_pairing(uint32_t ms)
{
    s_pair_until_us = esp_timer_get_time() + (int64_t)ms * 1000;
    ESP_LOGI(TAG, "pairing open for %u s", (unsigned)(ms / 1000));
}

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[LF_BLE_STATE_LEN];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        size_t n = lf_link_read_state(&s_link, buf);
        xSemaphoreGive(s_lock);
        return n && os_mbuf_append(ctxt->om, buf, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;
    uint8_t buf[64];
    uint16_t len = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) > sizeof buf)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &len) != 0)
        return BLE_ATT_ERR_UNLIKELY;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    lf_link_status st = lf_link_write(&s_link, (lf_chr)(intptr_t)arg, buf, len, now_ms());
    xSemaphoreGive(s_lock);
    switch (st) {
    case LF_LINK_OK: return 0;
    case LF_LINK_BAD_LENGTH: return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    default: return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &u_service.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = &u_config.u, .access_cb = access_cb, .arg = (void *)(intptr_t)LF_CHR_CONFIG,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &u_command.u, .access_cb = access_cb, .arg = (void *)(intptr_t)LF_CHR_COMMAND,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {.uuid = &u_state.u, .access_cb = access_cb, .val_handle = &s_state_handle,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY},
            {.uuid = &u_events.u, .access_cb = access_cb, .val_handle = &s_events_handle,
             .flags = BLE_GATT_CHR_F_NOTIFY},
            {.uuid = &u_ack.u, .access_cb = access_cb, .arg = (void *)(intptr_t)LF_CHR_ACK,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
            {0},
        },
    },
    {0},
};

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            advertise();
            return 0;
        }
        s_conn = event->connect.conn_handle;
        s_subscribed = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        lf_link_set_connected(&s_link, 1);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "connected");
        ble_gap_security_initiate(s_conn); /* a bonded phone encrypts at once; a new one pairs */
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        lf_link_set_connected(&s_link, 0);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "disconnected (reason %d)", event->disconnect.reason);
        advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0 || ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0) {
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        /* Outside the pairing window only an already bonded phone may stay. */
        if (!pairing_open() && !desc.sec_state.bonded) {
            ESP_LOGW(TAG, "unbonded phone outside the pairing window: dropped");
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        unsigned bit = 0;
        if (event->subscribe.attr_handle == s_state_handle)
            bit = LF_NOTIFY_STATE;
        else if (event->subscribe.attr_handle == s_events_handle)
            bit = LF_NOTIFY_EVENTS;
        if (bit) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (event->subscribe.cur_notify)
                s_subscribed |= bit;
            else
                s_subscribed &= ~bit;
            if (event->subscribe.cur_notify)
                lf_link_resubscribed(&s_link, (lf_notify_mask)bit);
            xSemaphoreGive(s_lock);
        }
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (!pairing_open())
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu %d", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&u_service;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0)
        return;
    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&rsp) != 0)
        return;
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY)
        ESP_LOGE(TAG, "advertising failed: %d", rc);
}

static void on_sync(void)
{
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0, &s_own_addr_type));
    int bonds = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonds);
    if (bonds == 0)
        gatt_open_pairing(PAIR_OPEN_MS); /* a fresh device can be paired right after power-up */
    ESP_LOGI(TAG, "advertising as %s, %d bonded phone(s)", s_name, bonds);
    advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset, reason %d", reason); }

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* Sends queued notifications and watches the pair button. */
static void pump_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_LF_PIN_PAIR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    uint32_t held_ms = 0;
    int opened = 0, cleared = 0;
    for (;;) {
        if (gpio_get_level(CONFIG_LF_PIN_PAIR) == 0) {
            held_ms += 20;
            if (held_ms >= PAIR_HOLD_MS && !opened) {
                opened = 1;
                gatt_open_pairing(PAIR_OPEN_MS);
            }
            if (held_ms >= CLEAR_HOLD_MS && !cleared) {
                cleared = 1;
                ble_store_clear();
                ESP_LOGW(TAG, "all bonds cleared");
            }
        } else {
            held_ms = 0;
            opened = cleared = 0;
        }

        for (int i = 0; i < 4 && s_conn != BLE_HS_CONN_HANDLE_NONE; i++) {
            lf_notification n;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int have = lf_link_next(&s_link, now_ms(), s_subscribed, &n);
            xSemaphoreGive(s_lock);
            if (!have)
                break;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(n.data, n.len);
            uint16_t handle = n.which == LF_NOTIFY_EVENTS ? s_events_handle : s_state_handle;
            if (!om || ble_gatts_notify_custom(s_conn, handle, om) != 0) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                lf_link_unsend(&s_link);
                xSemaphoreGive(s_lock);
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void gatt_start(const lf_link_ops *ops, SemaphoreHandle_t lock)
{
    s_lock = lock;
    lf_link_init(&s_link, ops);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BT));
    snprintf(s_name, sizeof s_name, "LF-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; /* just works, bonded; see docs/ble-protocol.md */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(s_name));
    ESP_ERROR_CHECK(ble_gatts_count_cfg(s_services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(s_services));
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    xTaskCreate(pump_task, "lf_pump", 3072, NULL, 5, NULL);
}
