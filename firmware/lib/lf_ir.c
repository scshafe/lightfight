#include "lf_ir.h"

uint8_t lf_ir_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

static uint8_t crc_of(uint16_t payload)
{
    uint8_t bytes[2] = {(uint8_t)(payload >> 8), (uint8_t)payload};
    return lf_ir_crc8(bytes, 2);
}

lf_ir_status lf_ir_pack(const lf_ir_frame *f, uint32_t *word)
{
    if (f->shooter > 63 || f->team > 7 || f->damage > 7 || f->seq > 15)
        return LF_IR_ERR_RANGE;
    uint16_t payload = (uint16_t)((f->shooter << 10) | (f->team << 7) | (f->damage << 4) | f->seq);
    *word = ((uint32_t)payload << 8) | crc_of(payload);
    return LF_IR_OK;
}

lf_ir_status lf_ir_unpack(uint32_t word, lf_ir_frame *f)
{
    if (word >> LF_IR_BITS)
        return LF_IR_ERR_RANGE;
    uint16_t payload = (uint16_t)(word >> 8);
    if (crc_of(payload) != (uint8_t)word)
        return LF_IR_ERR_CRC;
    f->shooter = (uint8_t)(payload >> 10);
    f->team = (payload >> 7) & 7;
    f->damage = (payload >> 4) & 7;
    f->seq = payload & 15;
    return LF_IR_OK;
}

lf_ir_status lf_ir_build_timings(const lf_ir_frame *f, uint16_t out[LF_IR_TIMINGS])
{
    uint32_t word;
    lf_ir_status s = lf_ir_pack(f, &word);
    if (s != LF_IR_OK)
        return s;
    size_t i = 0;
    out[i++] = LF_IR_HEADER_MARK_US;
    out[i++] = LF_IR_HEADER_SPACE_US;
    for (int b = (int)LF_IR_BITS - 1; b >= 0; b--) {
        out[i++] = LF_IR_MARK_US;
        out[i++] = (word >> b) & 1 ? LF_IR_SPACE1_US : LF_IR_SPACE0_US;
    }
    out[i++] = LF_IR_MARK_US;
    return LF_IR_OK;
}

static int near(uint16_t got, uint16_t nominal)
{
    uint32_t tol = (uint32_t)nominal * LF_IR_TOLERANCE_PERCENT / 100;
    return got >= nominal - tol && got <= nominal + tol;
}

lf_ir_status lf_ir_parse_timings(const uint16_t *t, size_t n, lf_ir_frame *f)
{
    if (n != LF_IR_TIMINGS)
        return LF_IR_ERR_LENGTH;
    if (!near(t[0], LF_IR_HEADER_MARK_US) || !near(t[1], LF_IR_HEADER_SPACE_US))
        return LF_IR_ERR_TIMING;
    uint32_t word = 0;
    for (size_t b = 0; b < LF_IR_BITS; b++) {
        uint16_t mark = t[2 + 2 * b], space = t[3 + 2 * b];
        if (!near(mark, LF_IR_MARK_US))
            return LF_IR_ERR_TIMING;
        word <<= 1;
        if (near(space, LF_IR_SPACE1_US))
            word |= 1;
        else if (!near(space, LF_IR_SPACE0_US))
            return LF_IR_ERR_TIMING;
    }
    if (!near(t[LF_IR_TIMINGS - 1], LF_IR_MARK_US))
        return LF_IR_ERR_TIMING;
    return lf_ir_unpack(word, f);
}
