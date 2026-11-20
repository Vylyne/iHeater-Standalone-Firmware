#include "link_regs.h"
#include "config.h"
#include "version.h"
#include "modbus_rtu.h"
#include "fault_policy.h"
#include <math.h>

#define RCC_PINRSTF  (1u << 26)
#define RCC_PORRSTF  (1u << 27)
#define RCC_SFTRSTF  (1u << 28)
#define RCC_IWDGRSTF (1u << 29)

#define PROTO_VERSION 1u

int16_t link_temp_c10(float c)
{
    if (!isfinite(c)) return INT16_MIN;
    float t = roundf(c * 10.0f);
    if (t > 32767.0f) return INT16_MAX;
    if (t < -32767.0f) return -32767; /* keep 0x8000 reserved for "unreadable" */
    return (int16_t)t;
}

uint8_t link_reset_cause(uint32_t csr)
{
    if (csr & RCC_PORRSTF) return LINK_RESET_POWER_ON;
    if (csr & RCC_IWDGRSTF) return LINK_RESET_WATCHDOG;
    if (csr & RCC_SFTRSTF) return LINK_RESET_SOFTWARE;
    if (csr & RCC_PINRSTF) return LINK_RESET_PIN;
    return LINK_RESET_UNKNOWN;
}

static uint16_t temp_reg(float c) { return (uint16_t)link_temp_c10(c); }

static uint16_t pwm_permille(float pct)
{
    if (!isfinite(pct)) return 0;
    float p = roundf(pct * 10.0f);
    if (p < 0.0f) return 0;
    if (p > 1000.0f) return 1000;
    return (uint16_t)p;
}

uint8_t link_regs_read_input(void *ctx, uint16_t addr, uint16_t *out)
{
    const LinkSnapshot *s = &((const LinkRegs *)ctx)->snap;
    uint16_t v;
    switch (addr) {
    case 0x00: v = PROTO_VERSION; break;
    case 0x01: v = (uint16_t)((VERSION_MAJOR << 8) | VERSION_MINOR); break;
    case 0x02: v = VERSION_PATCH; break;
    case 0x03: v = s->state; break;
    case 0x04: v = s->active_fault; break;
    case 0x05: v = (fault_evaluate_clear(s->active_fault, s->active_fault, s->clears_left, s->safe_ok) == CLEAR_OK) ? 1 : 0; break;
    case 0x06: v = fault_clear_allow_mask(); break;
    case 0x07: v = s->clears_left; break;
    case 0x08: v = s->last_clear_result; break;
    case 0x09: v = temp_reg(s->air_c); break;
    case 0x0A: v = temp_reg(s->heater_c); break;
    case 0x0B: v = s->aux_valid ? temp_reg(s->aux_c) : 0x8000u; break;
    case 0x0C: v = temp_reg(s->air_target_c); break;
    case 0x0D: v = temp_reg(s->heater_setpoint_c); break;
    case 0x0E: v = pwm_permille(s->pwm_pct); break;
    case 0x0F: v = s->fan_on ? 1 : 0; break;
    case 0x10: v = (uint16_t)(s->uptime_s & 0xFFFFu); break;
    case 0x11: v = (uint16_t)(s->uptime_s >> 16); break;
    case 0x12: v = s->link_loss_count; break;
    case 0x13: v = s->hold_remaining_s; break;
    case 0x14: v = s->reset_cause; break;
    case 0x15: v = s->last_fault; break;
    default: return MODBUS_EX_ILLEGAL_ADDRESS;
    }
    *out = v;
    return 0;
}

uint8_t link_regs_read_holding(void *ctx, uint16_t addr, uint16_t *out)
{
    const LinkSnapshot *s = &((const LinkRegs *)ctx)->snap;
    uint16_t v;
    switch (addr) {
    case 0x0100: v = temp_reg(s->air_target_c); break;
    case 0x0101: v = 0; break;
    case 0x0102: v = 0; break;
    case 0x0200: v = (uint16_t)(MAX_TEMP * 10); break;
    case 0x0201: v = (uint16_t)(MAX_AIR_TEMP * 10); break;
    case 0x0202: v = (uint16_t)(LINK_MIN_TARGET * 10); break;
    case 0x0203: v = (uint16_t)(LINK_MAX_TARGET * 10); break;
    case 0x0204: v = (uint16_t)LINK_HOLD_S; break;
    case 0x0205: v = (uint16_t)(DELTA * 10); break;
    case 0x0206: v = temp_reg(FAN_ON_TEMP); break;
    case 0x0207: v = temp_reg(FAN_OFF_TEMP); break;
    case 0x0208: v = temp_reg(FAN_ON_ACTIVE_MODE_TEMP); break;
    case 0x0209: v = temp_reg(LINK_CLEAR_MARGIN); break;
    default: return MODBUS_EX_ILLEGAL_ADDRESS;
    }
    *out = v;
    return 0;
}

uint8_t link_regs_write_holding(void *ctx, uint16_t addr, uint16_t value)
{
    LinkRegs *r = (LinkRegs *)ctx;
    switch (addr) {
    case 0x0100:
        if (r->snap.active_fault != 0) return MODBUS_EX_ILLEGAL_ADDRESS;
        if (value != 0 && (value < LINK_MIN_TARGET * 10u || value > LINK_MAX_TARGET * 10u))
            return MODBUS_EX_ILLEGAL_VALUE;
        r->cmd.target_written = true;
        r->cmd.target_c10 = value;
        r->snap.air_target_c = (float)value / 10.0f;
        return 0;
    case 0x0101: {
        ClearResult res = fault_evaluate_clear(r->snap.active_fault, value, r->snap.clears_left, r->snap.safe_ok);
        r->snap.last_clear_result = (uint8_t)res;
        if (res != CLEAR_OK) return MODBUS_EX_ILLEGAL_VALUE;
        r->cmd.clear_approved = true;
        return 0;
    }
    default:
        return MODBUS_EX_ILLEGAL_ADDRESS;
    }
}
