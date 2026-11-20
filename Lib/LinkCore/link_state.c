#include "link_state.h"
#include "config.h"

#define HOLD_MS ((uint32_t)LINK_HOLD_S * 1000U)

void link_state_init(LinkState *s)
{
    s->last_activity_ms = 0;
    s->hold_start_ms = 0;
    s->loss_count = 0;
    s->seen_activity = false;
    s->in_hold = false;
    s->expired = false;
}

void link_state_activity(LinkState *s, uint32_t now_ms)
{
    s->last_activity_ms = now_ms;
    s->seen_activity = true;
    s->in_hold = false;
    s->expired = false;
}

LinkEvent link_state_tick(LinkState *s, uint32_t now_ms)
{
    if (!s->seen_activity || s->expired) return LINK_EVT_NONE;
    if (!s->in_hold) {
        if ((uint32_t)(now_ms - s->last_activity_ms) < LINK_SILENCE_MS) return LINK_EVT_NONE;
        s->in_hold = true;
        s->hold_start_ms = s->last_activity_ms + LINK_SILENCE_MS;
        s->loss_count++;
        return LINK_EVT_HOLD_STARTED;
    }
    if ((uint32_t)(now_ms - s->hold_start_ms) >= HOLD_MS) {
        s->in_hold = false;
        s->expired = true;
        return LINK_EVT_HOLD_EXPIRED;
    }
    return LINK_EVT_NONE;
}

uint16_t link_state_hold_remaining_s(const LinkState *s, uint32_t now_ms)
{
    if (!s->in_hold) return 0;
    uint32_t elapsed = (uint32_t)(now_ms - s->hold_start_ms);
    if (elapsed >= HOLD_MS) return 0;
    return (uint16_t)((HOLD_MS - elapsed + 999U) / 1000U);
}
