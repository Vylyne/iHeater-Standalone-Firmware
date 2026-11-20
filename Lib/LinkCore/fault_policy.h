#ifndef FAULT_POLICY_H
#define FAULT_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CLEAR_NONE = 0,
    CLEAR_OK = 1,
    CLEAR_NOT_ALLOWED = 2,
    CLEAR_UNSAFE = 3,
    CLEAR_NO_CLEARS_LEFT = 4,
    CLEAR_CODE_MISMATCH = 5,
    CLEAR_NO_FAULT = 6
} ClearResult;

typedef struct {
    bool safe;
    uint32_t safe_since_ms;
} SafeTracker;

/* Bit n set = fault code n may be cleared remotely. Codes above 15 never. */
uint16_t fault_clear_allow_mask(void);
bool fault_remote_clearable(uint8_t code);

/* True only for finite readings above the open-circuit threshold and at
 * least LINK_CLEAR_MARGIN below the overheat limits. */
bool fault_readings_safe(float air_c, float heater_c);

void safe_tracker_init(SafeTracker *t);
/* Call on every service pass, whether or not a frame arrived: the tracker only
 * measures continuity if it sees every reading, and a gap in calls would let a
 * stale "safe" state count as continuous. */
void safe_tracker_update(SafeTracker *t, float air_c, float heater_c, uint32_t now_ms);
/* True when readings have been safe continuously for >= LINK_CLEAR_SAFE_MS. */
bool safe_tracker_ok(const SafeTracker *t, uint32_t now_ms);

/* Checks in order: no fault, code mismatch, not allowed, no clears left, unsafe. */
ClearResult fault_evaluate_clear(uint8_t active_fault, uint16_t requested, uint8_t clears_left, bool safe_ok);

#endif
