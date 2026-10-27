/* Battery gauge: an ADC reading of the 1:2 divider on the cell becomes a
 * percentage (docs/hardware.md, "Battery sense"). Pure arithmetic; the ADC
 * read is in the ESP-IDF glue. */
#ifndef LF_BATTERY_H
#define LF_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

/* Below this the cell is taken as absent (a board on USB with no pack): the
 * sample is ignored. */
#define LF_BATTERY_MIN_MV 2500

/* Cell voltage from the divider midpoint: two equal resistors, so x2, times a
 * calibration factor in parts per thousand (1000 = none). */
uint32_t lf_battery_cell_mv(uint32_t pin_mv, uint32_t cal_permille);

/* Resting 1S Li-ion/LiPo curve (typical; calibrate on the bench, B9). */
uint8_t lf_battery_pct(uint32_t cell_mv);

typedef struct {
    uint32_t mv_x8;     /* running average of the cell voltage, x8 */
    bool primed;
} lf_battery;

void lf_battery_init(lf_battery *b);
/* Feed one cell-voltage sample (about 1 per second). The average moves 1/8 of
 * the way to each sample, so a transmit pulse or a motor start does not show. */
void lf_battery_sample(lf_battery *b, uint32_t cell_mv);
/* Percent from the average; 100 until a first valid sample arrives. */
uint8_t lf_battery_percent(const lf_battery *b);
uint32_t lf_battery_mv(const lf_battery *b);

#endif
