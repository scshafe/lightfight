/* The BLE side of a gear device: a NimBLE peripheral that exposes the
 * characteristics of docs/ble-protocol.md and hands every byte to lf_link
 * (portable, tested on the host). */
#ifndef GATT_H
#define GATT_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lf_link.h"

/* Starts advertising as LF-<4 hex>. `lock` serializes the device's own task
 * with the BLE callbacks: gatt.c takes it around every call into `ops`, and the
 * device takes it around everything else that touches the same state. */
void gatt_start(const lf_link_ops *ops, SemaphoreHandle_t lock);

/* Opens the window in which a new phone may pair (the pair button does this
 * too, and so does a first boot with no bonded phone). */
void gatt_open_pairing(uint32_t ms);

#endif
