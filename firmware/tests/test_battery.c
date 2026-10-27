#include "../lib/lf_battery.h"
#include "lf_test.h"

static void test_curve(void)
{
    CHECK(lf_battery_pct(4300) == 100 && lf_battery_pct(4200) == 100);
    CHECK(lf_battery_pct(3300) == 0 && lf_battery_pct(3000) == 0);
    CHECK(lf_battery_pct(3830) == 25);
    CHECK(lf_battery_pct(4130) == 93); /* halfway between 95 and 90, rounded */
    uint8_t prev = 0;
    for (uint32_t mv = 3000; mv <= 4300; mv++) { /* never decreases with voltage */
        uint8_t p = lf_battery_pct(mv);
        CHECK(p >= prev && p <= 100);
        prev = p;
    }
}

static void test_divider(void)
{
    CHECK(lf_battery_cell_mv(2000, 1000) == 4000);
    CHECK(lf_battery_cell_mv(2000, 1010) == 4040); /* +1% calibration */
}

static void test_filter(void)
{
    lf_battery b;
    lf_battery_init(&b);
    CHECK(lf_battery_percent(&b) == 100); /* nothing measured yet */
    lf_battery_sample(&b, 0);             /* no cell: ignored */
    CHECK(lf_battery_percent(&b) == 100 && !b.primed);
    lf_battery_sample(&b, 3830);
    CHECK(lf_battery_mv(&b) == 3830 && lf_battery_percent(&b) == 25);
    lf_battery_sample(&b, 3300); /* a sag moves it only a little */
    CHECK(lf_battery_mv(&b) > 3700);
    for (int i = 0; i < 100; i++)
        lf_battery_sample(&b, 3300);
    CHECK(lf_battery_mv(&b) <= 3310 && lf_battery_percent(&b) <= 1);
    for (int i = 0; i < 200; i++)
        lf_battery_sample(&b, 4200);
    CHECK(lf_battery_percent(&b) >= 99);
}

int main(void)
{
    test_curve();
    test_divider();
    test_filter();
    LF_DONE("test_battery");
}
