#include <stdio.h>

#include "lf_log.h"

static const char *reason_name(lf_ignore_reason r)
{
    switch (r) {
    case LF_IGNORE_INACTIVE: return "inactive";
    case LF_IGNORE_SELF: return "self";
    case LF_IGNORE_DEAD: return "dead";
    case LF_IGNORE_DUPLICATE: return "duplicate";
    case LF_IGNORE_FRIENDLY: return "friendly";
    default: return "none";
    }
}

static const char *status_name(lf_ir_status s)
{
    switch (s) {
    case LF_IR_ERR_RANGE: return "range";
    case LF_IR_ERR_LENGTH: return "length";
    case LF_IR_ERR_TIMING: return "timing";
    case LF_IR_ERR_CRC: return "crc";
    default: return "ok";
    }
}

size_t lf_log_format(const lf_log_entry *e, char *buf, size_t size)
{
    int n = 0;
    switch (e->kind) {
    case LF_LOG_SHOT:
        n = snprintf(buf, size, "LFLOG t_ms=%lu ev=shot shooter=%u team=%u seq=%u",
                     (unsigned long)e->t_ms, e->frame.shooter, e->frame.team, e->frame.seq);
        break;
    case LF_LOG_HIT:
        n = snprintf(buf, size, "LFLOG t_ms=%lu ev=hit sensor=%d shooter=%u seq=%u dmg=%u health=%u",
                     (unsigned long)e->t_ms, e->sensor, e->frame.shooter, e->frame.seq, e->damage, e->health);
        break;
    case LF_LOG_DROP:
        n = snprintf(buf, size, "LFLOG t_ms=%lu ev=drop sensor=%d shooter=%u seq=%u reason=%s",
                     (unsigned long)e->t_ms, e->sensor, e->frame.shooter, e->frame.seq, reason_name(e->reason));
        break;
    case LF_LOG_BAD:
        n = snprintf(buf, size, "LFLOG t_ms=%lu ev=bad sensor=%d why=%s",
                     (unsigned long)e->t_ms, e->sensor, status_name(e->status));
        break;
    case LF_LOG_RX:
        n = snprintf(buf, size, "LFLOG t_ms=%lu ev=rx sensor=%d glitches=%lu overflows=%lu stuck=%lu late=%lu frames=%lu",
                     (unsigned long)e->t_ms, e->sensor, (unsigned long)e->counters[0], (unsigned long)e->counters[1],
                     (unsigned long)e->counters[2], (unsigned long)e->counters[3], (unsigned long)e->counters[4]);
        break;
    }
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}
