#ifndef LINK_STATE_H
#define LINK_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { LINK_EVT_NONE = 0, LINK_EVT_HOLD_STARTED, LINK_EVT_HOLD_EXPIRED } LinkEvent;

typedef struct {
    uint32_t last_activity_ms, hold_start_ms;
    uint16_t loss_count;
    bool seen_activity, in_hold, expired;
} LinkState;

void link_state_init(LinkState *s);
/* Record a valid frame; ends any hold or expiry. */
void link_state_activity(LinkState *s, uint32_t now_ms);
/* Call every loop pass. Each event is returned once per outage. */
LinkEvent link_state_tick(LinkState *s, uint32_t now_ms);
/* Whole seconds left in the hold window, rounded up; 0 unless in_hold. */
uint16_t link_state_hold_remaining_s(const LinkState *s, uint32_t now_ms);

#endif
