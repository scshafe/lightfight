/* Battery sense: samples the divider on LF_PIN_BATTERY once a second and keeps
 * a percentage (lf_battery). */
#ifndef LF_IDF_BATTERY_H
#define LF_IDF_BATTERY_H

#include <stdint.h>

void battery_start(void);
uint8_t battery_percent(void); /* 100 until the first valid sample */
uint32_t battery_mv(void);     /* cell voltage, 0 until sampled */

#endif
