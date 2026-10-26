/* The one-line log format of the bench and field tests (docs/hit-log.md),
 * read by tools/lfhits.py. Portable so the format is tested on the host. */
#ifndef LF_LOG_H
#define LF_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "lf_ir.h"
#include "lf_rules.h"

typedef enum {
    LF_LOG_SHOT,  /* a blaster sent a frame */
    LF_LOG_HIT,   /* a hub counted a hit */
    LF_LOG_DROP,  /* a hub read a valid frame and did not count it */
    LF_LOG_BAD,   /* a hub received something that is not a valid frame */
    LF_LOG_RX     /* a hub's receiver counters, once in a while */
} lf_log_kind;

typedef struct {
    lf_log_kind kind;
    uint32_t t_ms;
    int sensor;                 /* -1 when not applicable */
    lf_ir_frame frame;          /* SHOT, HIT, DROP */
    uint8_t damage;             /* HIT: points applied */
    uint16_t health;            /* HIT: health after */
    lf_ignore_reason reason;    /* DROP */
    lf_ir_status status;        /* BAD */
    uint32_t counters[5];       /* RX: glitches, overflows, stuck, late, frames */
} lf_log_entry;

/* Writes "LFLOG ..." (no newline) and returns its length, or 0 if it does not fit. */
size_t lf_log_format(const lf_log_entry *e, char *buf, size_t size);

#endif
