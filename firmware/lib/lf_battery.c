#include "lf_battery.h"

static const struct { uint16_t mv; uint8_t pct; } curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 75}, {3980, 65}, {3950, 55},
    {3910, 45},  {3870, 35}, {3830, 25}, {3790, 15}, {3730, 8},  {3650, 4},  {3500, 1},
    {3300, 0},
};
#define N (sizeof curve / sizeof curve[0])

uint32_t lf_battery_cell_mv(uint32_t pin_mv, uint32_t cal_permille) { return pin_mv * 2u * cal_permille / 1000u; }

uint8_t lf_battery_pct(uint32_t mv)
{
    if (mv >= curve[0].mv)
        return 100;
    if (mv <= curve[N - 1].mv)
        return 0;
    for (unsigned i = 1; i < N; i++) {
        if (mv >= curve[i].mv) {
            uint32_t span = (uint32_t)curve[i - 1].mv - curve[i].mv;
            uint32_t rise = (uint32_t)curve[i - 1].pct - curve[i].pct;
            return (uint8_t)(curve[i].pct + ((mv - curve[i].mv) * rise + span / 2) / span);
        }
    }
    return 0;
}

void lf_battery_init(lf_battery *b)
{
    b->mv_x8 = 0;
    b->primed = false;
}

void lf_battery_sample(lf_battery *b, uint32_t cell_mv)
{
    if (cell_mv < LF_BATTERY_MIN_MV)
        return;
    if (!b->primed) {
        b->mv_x8 = cell_mv * 8u;
        b->primed = true;
        return;
    }
    b->mv_x8 = b->mv_x8 - b->mv_x8 / 8u + cell_mv;
}

uint32_t lf_battery_mv(const lf_battery *b) { return b->mv_x8 / 8u; }

uint8_t lf_battery_percent(const lf_battery *b) { return b->primed ? lf_battery_pct(lf_battery_mv(b)) : 100; }
