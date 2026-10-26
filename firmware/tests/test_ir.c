#include <stdlib.h>
#include <string.h>

#include "../lib/lf_ir.h"
#include "lf_test.h"

#ifndef VECTORS
#define VECTORS "../protocol/ir-vectors.csv"
#endif

static void test_vectors(void)
{
    FILE *fp = fopen(VECTORS, "r");
    CHECK(fp != NULL);
    if (!fp)
        return;
    char line[128];
    int rows = 0;
    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        unsigned s, t, d, q;
        unsigned long hex;
        CHECK(sscanf(line, "%u,%u,%u,%u,%lx", &s, &t, &d, &q, &hex) == 5);
        lf_ir_frame f = {(uint8_t)s, (uint8_t)t, (uint8_t)d, (uint8_t)q}, g;
        uint32_t word = 0;
        CHECK(lf_ir_pack(&f, &word) == LF_IR_OK);
        CHECK(word == hex);
        CHECK(lf_ir_unpack((uint32_t)hex, &g) == LF_IR_OK);
        CHECK(memcmp(&f, &g, sizeof f) == 0);
        rows++;
    }
    fclose(fp);
    CHECK(rows >= 5);
}

static void test_roundtrip_exhaustive(void)
{
    uint16_t t[LF_IR_TIMINGS];
    for (unsigned s = 0; s < 64; s++)
        for (unsigned tm = 0; tm < 8; tm++)
            for (unsigned d = 0; d < 8; d++)
                for (unsigned q = 0; q < 16; q++) {
                    lf_ir_frame f = {(uint8_t)s, (uint8_t)tm, (uint8_t)d, (uint8_t)q}, g;
                    CHECK(lf_ir_build_timings(&f, t) == LF_IR_OK);
                    CHECK(lf_ir_parse_timings(t, LF_IR_TIMINGS, &g) == LF_IR_OK);
                    CHECK(memcmp(&f, &g, sizeof f) == 0);
                }
}

static void test_range(void)
{
    uint32_t w;
    uint16_t t[LF_IR_TIMINGS];
    lf_ir_frame bad[] = {{64, 0, 0, 0}, {0, 8, 0, 0}, {0, 0, 8, 0}, {0, 0, 0, 16}};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(lf_ir_pack(&bad[i], &w) == LF_IR_ERR_RANGE);
        CHECK(lf_ir_build_timings(&bad[i], t) == LF_IR_ERR_RANGE);
    }
}

/* CRC-8 poly 0x07 has Hamming distance 4 at this length: every 1, 2 and 3 bit
 * error in a frame must be rejected. */
static void test_bit_errors_rejected(void)
{
    const uint32_t base = 0x1539B9;
    lf_ir_frame g;
    for (int i = 0; i < 24; i++) {
        CHECK(lf_ir_unpack(base ^ (1u << i), &g) == LF_IR_ERR_CRC);
        for (int j = i + 1; j < 24; j++) {
            CHECK(lf_ir_unpack(base ^ (1u << i) ^ (1u << j), &g) == LF_IR_ERR_CRC);
            for (int k = j + 1; k < 24; k++)
                CHECK(lf_ir_unpack(base ^ (1u << i) ^ (1u << j) ^ (1u << k), &g) == LF_IR_ERR_CRC);
        }
    }
    CHECK(lf_ir_unpack(0x1000000, &g) == LF_IR_ERR_RANGE);
}

static void test_timing_tolerance(void)
{
    lf_ir_frame f = {42, 1, 4, 3}, g;
    uint16_t t[LF_IR_TIMINGS], u[LF_IR_TIMINGS];
    lf_ir_build_timings(&f, t);
    /* every duration stretched or shrunk by 20% still parses */
    for (int pct = 80; pct <= 120; pct += 40) {
        for (size_t i = 0; i < LF_IR_TIMINGS; i++)
            u[i] = (uint16_t)(t[i] * pct / 100);
        CHECK(lf_ir_parse_timings(u, LF_IR_TIMINGS, &g) == LF_IR_OK);
        CHECK(memcmp(&f, &g, sizeof f) == 0);
    }
    /* 30% off, anywhere, does not */
    for (size_t i = 0; i < LF_IR_TIMINGS; i++) {
        memcpy(u, t, sizeof t);
        u[i] = (uint16_t)(t[i] * 130 / 100);
        CHECK(lf_ir_parse_timings(u, LF_IR_TIMINGS, &g) == LF_IR_ERR_TIMING);
        u[i] = (uint16_t)(t[i] * 70 / 100);
        CHECK(lf_ir_parse_timings(u, LF_IR_TIMINGS, &g) == LF_IR_ERR_TIMING);
    }
    CHECK(lf_ir_parse_timings(t, LF_IR_TIMINGS - 1, &g) == LF_IR_ERR_LENGTH);
    CHECK(lf_ir_parse_timings(t, LF_IR_TIMINGS + 1, &g) == LF_IR_ERR_LENGTH);
    CHECK(lf_ir_parse_timings(t, 0, &g) == LF_IR_ERR_LENGTH);
}

/* Ambient IR and noise: random durations, and valid frames with one data bit
 * flipped, must never produce a frame. */
static void test_noise(void)
{
    srand(12345);
    uint16_t t[LF_IR_TIMINGS];
    lf_ir_frame g;
    for (int n = 0; n < 200000; n++) {
        for (size_t i = 0; i < LF_IR_TIMINGS; i++)
            t[i] = (uint16_t)(rand() % 4000);
        CHECK(lf_ir_parse_timings(t, LF_IR_TIMINGS, &g) != LF_IR_OK);
    }
    /* well-formed timings of random 24-bit words: accepted only if the CRC holds */
    int accepted = 0;
    for (int n = 0; n < 200000; n++) {
        uint32_t w = (uint32_t)rand() & 0xFFFFFF;
        size_t k = 0;
        t[k++] = LF_IR_HEADER_MARK_US;
        t[k++] = LF_IR_HEADER_SPACE_US;
        for (int b = 23; b >= 0; b--) {
            t[k++] = LF_IR_MARK_US;
            t[k++] = (w >> b) & 1 ? LF_IR_SPACE1_US : LF_IR_SPACE0_US;
        }
        t[k++] = LF_IR_MARK_US;
        lf_ir_frame h;
        int ok = lf_ir_parse_timings(t, k, &h) == LF_IR_OK;
        accepted += ok;
        CHECK(ok == (lf_ir_unpack(w, &g) == LF_IR_OK));
    }
    /* about 1 in 256 random words has a valid CRC */
    CHECK(accepted > 200000 / 256 / 2 && accepted < 200000 / 256 * 2);
}

int main(void)
{
    test_vectors();
    test_roundtrip_exhaustive();
    test_range();
    test_bit_errors_rejected();
    test_timing_tolerance();
    test_noise();
    LF_DONE("test_ir");
}
