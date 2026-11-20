#include "fault_policy.h"
#include "config.h"
#include <math.h>

uint16_t fault_clear_allow_mask(void)
{
    uint16_t mask = 0;
#define X(code, remote_clear) \
    if ((remote_clear) && (code) >= 0 && (code) <= 15) mask |= (uint16_t)(1U << ((code) & 15));
    FAULT_TABLE(X)
#undef X
    return mask;
}

bool fault_remote_clearable(uint8_t code)
{
    if (code > 15U) return false;
    return (fault_clear_allow_mask() >> code) & 1U;
}

bool fault_readings_safe(float air_c, float heater_c)
{
    if (!isfinite(air_c) || !isfinite(heater_c)) return false;
    /* main.c treats <= MIN_* as an open thermistor */
    if (air_c <= (float)MIN_AIR_TEMP || heater_c <= (float)MIN_TEMP) return false;
    if (air_c > (float)MAX_AIR_TEMP - LINK_CLEAR_MARGIN) return false;
    if (heater_c > (float)MAX_TEMP - LINK_CLEAR_MARGIN) return false;
    return true;
}

void safe_tracker_init(SafeTracker *t)
{
    t->safe = false;
    t->safe_since_ms = 0;
}

void safe_tracker_update(SafeTracker *t, float air_c, float heater_c, uint32_t now_ms)
{
    if (!fault_readings_safe(air_c, heater_c)) {
        t->safe = false;
        return;
    }
    if (!t->safe) {
        t->safe = true;
        t->safe_since_ms = now_ms;
    }
}

bool safe_tracker_ok(const SafeTracker *t, uint32_t now_ms)
{
    return t->safe && (uint32_t)(now_ms - t->safe_since_ms) >= LINK_CLEAR_SAFE_MS;
}

ClearResult fault_evaluate_clear(uint8_t active_fault, uint16_t requested, uint8_t clears_left, bool safe_ok)
{
    if (active_fault == 0) return CLEAR_NO_FAULT;
    if (requested != active_fault) return CLEAR_CODE_MISMATCH;
    if (!fault_remote_clearable(active_fault)) return CLEAR_NOT_ALLOWED;
    if (clears_left == 0) return CLEAR_NO_CLEARS_LEFT;
    if (!safe_ok) return CLEAR_UNSAFE;
    return CLEAR_OK;
}
