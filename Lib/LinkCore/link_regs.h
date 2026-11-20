#ifndef LINK_REGS_H
#define LINK_REGS_H

#include <stdbool.h>
#include <stdint.h>

enum { LINK_DEV_BOOTING = 0, LINK_DEV_IDLE = 1, LINK_DEV_HEATING = 2, LINK_DEV_HOLD = 3, LINK_DEV_FAULT = 4 };
enum { LINK_RESET_UNKNOWN = 0, LINK_RESET_POWER_ON = 1, LINK_RESET_PIN = 2, LINK_RESET_SOFTWARE = 3, LINK_RESET_WATCHDOG = 4 };

typedef struct {
    uint8_t  state, active_fault, last_fault, clears_left, last_clear_result, reset_cause;
    bool     safe_ok, aux_valid, fan_on;
    float    air_c, heater_c, aux_c, air_target_c, heater_setpoint_c, pwm_pct;
    uint32_t uptime_s;
    uint16_t link_loss_count, hold_remaining_s;
} LinkSnapshot;

typedef struct { bool target_written; uint16_t target_c10; bool clear_approved; } LinkCommands;

typedef struct { LinkSnapshot snap; LinkCommands cmd; } LinkRegs;

/* Round to nearest (half away from zero); non-finite -> INT16_MIN (0x8000). */
int16_t link_temp_c10(float c);
/* Priority: power-on > watchdog > software > pin. */
uint8_t link_reset_cause(uint32_t rcc_csr);

/* ModbusHandlers-compatible; ctx is a LinkRegs *. 0 = ok, else exception code. */
uint8_t link_regs_read_input(void *ctx, uint16_t addr, uint16_t *out);
uint8_t link_regs_read_holding(void *ctx, uint16_t addr, uint16_t *out);
uint8_t link_regs_write_holding(void *ctx, uint16_t addr, uint16_t value);

#endif
