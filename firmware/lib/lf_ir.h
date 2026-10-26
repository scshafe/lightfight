/* lightfight IR frame codec (docs/ir-protocol.md). Portable C, no ESP-IDF. */
#ifndef LF_IR_H
#define LF_IR_H

#include <stddef.h>
#include <stdint.h>

#define LF_IR_CARRIER_HZ 38000u
#define LF_IR_BITS 24u
/* header mark+space, 24 x (mark+space), trailing mark */
#define LF_IR_TIMINGS (2u + 2u * LF_IR_BITS + 1u)

#define LF_IR_HEADER_MARK_US 3000u
#define LF_IR_HEADER_SPACE_US 1500u
#define LF_IR_MARK_US 560u
#define LF_IR_SPACE0_US 560u
#define LF_IR_SPACE1_US 1690u
#define LF_IR_TOLERANCE_PERCENT 25u

typedef struct {
    uint8_t shooter; /* 0..63 */
    uint8_t team;    /* 0..7 */
    uint8_t damage;  /* damage code 0..7 */
    uint8_t seq;     /* 0..15, wraps */
} lf_ir_frame;

typedef enum {
    LF_IR_OK = 0,
    LF_IR_ERR_RANGE,  /* a field of the frame is out of range */
    LF_IR_ERR_LENGTH, /* wrong number of timings */
    LF_IR_ERR_TIMING, /* a duration is out of tolerance */
    LF_IR_ERR_CRC
} lf_ir_status;

uint8_t lf_ir_crc8(const uint8_t *data, size_t len);

/* Frame <-> 24-bit word (shooter6 team3 damage3 seq4 crc8, MSB first). */
lf_ir_status lf_ir_pack(const lf_ir_frame *f, uint32_t *word);
lf_ir_status lf_ir_unpack(uint32_t word, lf_ir_frame *f);

/* Frame -> LF_IR_TIMINGS durations in microseconds, alternating mark and
 * space, starting with the header mark and ending with the trailing mark. */
lf_ir_status lf_ir_build_timings(const lf_ir_frame *f, uint16_t out[LF_IR_TIMINGS]);

/* Durations as measured by a demodulating receiver -> frame, or nothing. */
lf_ir_status lf_ir_parse_timings(const uint16_t *t, size_t n, lf_ir_frame *f);

#endif
