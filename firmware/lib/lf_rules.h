/* lightfight per-player rules engine: health, ammo, reload, respawn, shot rate
 * cap and hit de-duplication (docs/design.md, "How a hit travels"). Portable C;
 * the caller supplies time in milliseconds (a free-running uint32, wrap safe). */
#ifndef LF_RULES_H
#define LF_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lf_ir.h"

#define LF_DEDUPE_SLOTS 16

/* Three firing modes, each with its own trigger, magazine and reload
 * (docs/design.md, "Firing modes"). */
#define LF_MODES 3
enum { LF_MODE_LIGHT = 0, LF_MODE_HEAVY = 1, LF_MODE_SHOTGUN = 2 };

typedef struct {
    uint8_t damage_code;            /* code stamped on this mode's shots */
    bool auto_reload;               /* reload by itself when the magazine runs out */
    uint16_t magazine;              /* rounds per reload */
    uint32_t reload_ms;
    uint32_t fire_interval_ms;      /* this mode's cooldown after one of its shots */
} lf_mode_config;

typedef struct {
    uint8_t player;                 /* own shooter id, 0..63 */
    uint8_t team;                   /* 0..7 */
    uint8_t damage_table[8];        /* damage per code, in health points */
    uint16_t max_health;
    uint32_t respawn_ms;
    uint32_t gap_ms;                /* shot rate cap: minimum time between any two shots */
    uint32_t dedupe_window_ms;
    bool friendly_fire;
    lf_mode_config mode[LF_MODES];
} lf_rules_config;

typedef struct {
    uint8_t shooter, seq;
    uint32_t at_ms;
    bool used;
} lf_dedupe_slot;

typedef struct {
    lf_rules_config cfg;
    bool active;                    /* a match is running */
    bool alive;
    bool disabled;                  /* an outside source (the phone) switched the trigger off */
    uint16_t health;
    uint16_t ammo[LF_MODES];
    bool reloading[LF_MODES];
    uint32_t reload_done_ms[LF_MODES];
    uint32_t respawn_at_ms;
    bool fired[LF_MODES];           /* has shot since start (for the cooldowns) */
    uint32_t last_shot_ms[LF_MODES];
    bool fired_any;
    uint32_t last_any_ms;
    uint8_t last_mode;              /* the mode fired last: the reload button reloads it */
    uint8_t seq;                    /* sequence of the next shot */
    lf_dedupe_slot seen[LF_DEDUPE_SLOTS];
    unsigned seen_next;
} lf_rules;

/* Defaults from the design: health 100, 10 s respawn, 4 shots/s overall, 1 s
 * de-duplication, damage table 10/20/34/50/..; light: code 0, 30 rounds, 2 s
 * reload, 250 ms, auto-reload; heavy: code 3, 5 rounds, 3 s reload, 1.5 s
 * cooldown; shotgun: code 2, 2 rounds, 3.5 s reload, 1 s cooldown. */
lf_rules_config lf_rules_default_config(uint8_t player, uint8_t team);

void lf_rules_init(lf_rules *r, const lf_rules_config *cfg);
void lf_rules_start(lf_rules *r, uint32_t now_ms); /* full health and ammo, alive */
void lf_rules_stop(lf_rules *r);

typedef enum {
    LF_SHOT_OK = 0,
    LF_SHOT_INACTIVE,
    LF_SHOT_DEAD,
    LF_SHOT_DISABLED,
    LF_SHOT_BAD_MODE,
    LF_SHOT_RELOADING,
    LF_SHOT_NO_AMMO,
    LF_SHOT_RATE_LIMITED
} lf_shot_result;

/* A trigger pulled in `mode`. On LF_SHOT_OK, *frame is the frame to emit and
 * one round of that mode's magazine is spent. The rate cap and the mode's
 * cooldown are enforced here, not only in the emitter driver. */
lf_shot_result lf_rules_trigger(lf_rules *r, uint8_t mode, uint32_t now_ms, lf_ir_frame *frame);

/* Reload button, short press: reloads the mode fired last (the light mode
 * before any shot), if alive, active and not full. */
bool lf_rules_reload(lf_rules *r, uint32_t now_ms);
bool lf_rules_reload_mode(lf_rules *r, uint8_t mode, uint32_t now_ms);
/* Reload button held: reloads every mode that is not full. */
bool lf_rules_reload_all(lf_rules *r, uint32_t now_ms);
/* Bit m set while mode m reloads. */
uint8_t lf_rules_reloading_mask(const lf_rules *r);

typedef enum {
    LF_HIT_NONE = 0,       /* ignored, see reason */
    LF_HIT_DAMAGED,        /* took damage, still alive */
    LF_HIT_DOWN            /* took damage and went down */
} lf_hit_kind;

typedef enum {
    LF_IGNORE_NONE = 0,
    LF_IGNORE_INACTIVE,
    LF_IGNORE_SELF,
    LF_IGNORE_DEAD,
    LF_IGNORE_DUPLICATE,
    LF_IGNORE_FRIENDLY
} lf_ignore_reason;

typedef struct {
    lf_hit_kind kind;
    lf_ignore_reason reason;       /* meaningful when kind == LF_HIT_NONE */
    uint8_t shooter, seq, damage;  /* damage actually applied */
    uint16_t health_after;
} lf_hit;

/* A decoded frame arrived from a sensor. The same shot seen by several
 * sensors, or repeated, is one hit. */
lf_hit lf_rules_on_frame(lf_rules *r, uint32_t now_ms, const lf_ir_frame *frame);

typedef struct {
    bool reload_finished;           /* some reload finished (also an automatic one) */
    uint8_t reloaded_mask;          /* bit m: mode m's reload finished */
    bool respawned;
} lf_tick_events;

/* Call regularly: completes reloads and respawns. */
lf_tick_events lf_rules_tick(lf_rules *r, uint32_t now_ms);

#endif
