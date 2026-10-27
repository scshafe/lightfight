/* Rules engine <-> BLE messages, shared by the hub and the blaster. */
#ifndef LF_CONV_H
#define LF_CONV_H

#include "lf_ble.h"
#include "lf_rules.h"

/* The engine's configuration from a config message. The gap between any two
 * shots is `min_gap_ms` (the emitter gate's minimum): it is not a match
 * setting, and a mode's cooldown below it only means "as fast as the gap". */
lf_rules_config lf_conv_rules_config(const lf_ble_config *c, uint32_t min_gap_ms);

/* The state message from the engine; `disabled` is the blaster's switch. */
void lf_conv_state(const lf_rules *r, uint8_t battery_pct, lf_ble_state *out);

#endif
