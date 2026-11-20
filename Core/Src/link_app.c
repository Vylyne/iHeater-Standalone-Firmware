#include "config.h"

#if INPUT_MODE == INPUT_MODE_UART

#include "link_app.h"
#include "link_uart.h"
#include "main.h"
#include "modbus_rtu.h"
#include "link_regs.h"
#include "link_state.h"
#include "fault_policy.h"
#include <math.h>

/* Live values owned by main.c */
extern float air_temp;
extern float heater_temp;
extern float temperatures[];
extern float heater_setpoint;
extern float pwm; /* heater PID output, percent 0..100 */
void ClearErrorCode(void); /* main.c: writes ERROR_NONE to the fault page */
uint32_t LoadErrorCode(void); /* main.c: reads the fault page back */

static LinkRegs s_regs; /* persistent: the write handler owns snap.last_clear_result */
static LinkState s_link;
static SafeTracker s_safe;
static float s_target_c;
static uint32_t s_last_frame_tick;
static uint8_t s_reset_cause = LINK_RESET_UNKNOWN;
static bool s_reset_cause_read;

/* Remote-clear counter, last fault and last clear result. In .noinit (not
 * zeroed or copied by the startup code), so they survive the software reset
 * that follows a fault or a remote clear, and the watchdog and pin resets.
 * LinkApp_Init re-arms them on a power-on reset or when the magic is missing.
 * volatile: the decrement must be in RAM before NVIC_SystemReset. */
#define LINK_CLEARS_MAGIC 0x434C5253UL /* "CLRS" */
static volatile uint32_t s_clears_magic __attribute__((section(".noinit")));
static volatile uint32_t s_clears_left __attribute__((section(".noinit")));
static volatile uint32_t s_last_fault __attribute__((section(".noinit")));
static volatile uint32_t s_last_clear_result __attribute__((section(".noinit"))); /* ClearResult */

static const ModbusHandlers s_handlers = {
    link_regs_read_input,
    link_regs_read_holding,
    link_regs_write_holding,
    &s_regs,
};

/* Refresh the measurement and state fields. last_clear_result comes from
 * .noinit, so it persists until the next clear attempt, across the clear reset. */
static void refresh_snapshot(uint8_t active_fault, uint32_t now_ms)
{
    LinkSnapshot *s = &s_regs.snap;

    if (active_fault != 0U)
        s->state = LINK_DEV_FAULT;
    else if (s_link.in_hold)
        s->state = LINK_DEV_HOLD;
    else if (s_target_c > 0.0f)
        s->state = LINK_DEV_HEATING;
    else
        s->state = LINK_DEV_IDLE;

    s->active_fault = active_fault;
    s->last_fault = (uint8_t)s_last_fault;
    s->last_clear_result = (uint8_t)s_last_clear_result;
    s->clears_left = (uint8_t)s_clears_left;
    s->reset_cause = s_reset_cause;
    s->safe_ok = safe_tracker_ok(&s_safe, now_ms);

    s->air_c = air_temp;
    s->heater_c = heater_temp;
#if BOARD_REVISION == BOARD_REV_1_1
    s->aux_c = temperatures[2];
    /* An open or missing TH2 converts to a finite but impossible value
     * (down to -273 °C), so use the same open-circuit rule as the air
     * thermistor (main.c treats <= MIN_AIR_TEMP as open). */
    s->aux_valid = isfinite(s->aux_c) && s->aux_c > (float)MIN_AIR_TEMP;
#else
    s->aux_c = 0.0f;
    s->aux_valid = false; /* no TH2 channel on rev1.0 */
#endif

    s->air_target_c = s_target_c;
    s->heater_setpoint_c = heater_setpoint;
    /* With no target main.c forces the heater off but leaves pwm at the last
     * PID output, so report what actually reaches the heater. */
    s->pwm_pct = (s_target_c > 0.0f) ? pwm : 0.0f;
    s->fan_on = (FAN_GPIO_Port->ODR & FAN_Pin) != 0U;
    s->uptime_s = now_ms / 1000U;
    s->link_loss_count = s_link.loss_count;
    s->hold_remaining_s = link_state_hold_remaining_s(&s_link, now_ms);
}

void LinkApp_Init(uint8_t active_fault)
{
    if (!s_reset_cause_read)
    {
        /* Read once per boot, then clear, so the next reset reports only its own cause. */
        s_reset_cause = link_reset_cause(RCC->CSR);
        RCC->CSR |= RCC_CSR_RMVF;
        s_reset_cause_read = true;

        if (s_reset_cause == LINK_RESET_POWER_ON || s_clears_magic != LINK_CLEARS_MAGIC)
        {
            s_clears_left = LINK_REMOTE_CLEARS;
            s_last_fault = ERROR_NONE;
            s_last_clear_result = CLEAR_NONE;
            s_clears_magic = LINK_CLEARS_MAGIC;
        }
        else if (s_clears_left > LINK_REMOTE_CLEARS)
        {
            s_clears_left = 0U; /* corrupt count: fail closed, no remote clears until a power cycle */
        }

        if (s_last_clear_result > CLEAR_NO_FAULT)
        {
            s_last_clear_result = CLEAR_NONE; /* outside the ClearResult range: corrupt */
        }
    }

    if (active_fault != ERROR_NONE)
    {
        s_last_fault = active_fault; /* the fault loop's call: the code loaded at boot */
    }

    s_target_c = 0.0f;
    s_last_frame_tick = 0U;
    s_regs.cmd.target_written = false;
    s_regs.cmd.clear_approved = false;
    link_state_init(&s_link);
    safe_tracker_init(&s_safe);
    link_uart_init();
}

void LinkApp_Service(uint8_t active_fault)
{
    /* One time sample per pass, taken before the frame (link_state.h contract). */
    uint32_t now = HAL_GetTick();

    safe_tracker_update(&s_safe, air_temp, heater_temp, now);

    uint8_t req[LINK_RX_BUF];
    size_t req_len = 0U;
    if (link_uart_take_frame(req, &req_len))
    {
        /* Same rule modbus_handle_frame answers by; checked first so the frame
         * counts as activity before the reply snapshot (a frame that ends a
         * hold is not answered with state = hold). */
        if (modbus_frame_is_ours(req, req_len))
        {
            link_state_activity(&s_link, now);
        }

        refresh_snapshot(active_fault, now);

        uint8_t resp[LINK_TX_BUF];
        int resp_len = modbus_handle_frame(req, req_len, resp, sizeof resp, &s_handlers);

        /* The write handler records the outcome of every clear attempt in the
         * snapshot; keep the persistent copy in step. */
        s_last_clear_result = s_regs.snap.last_clear_result;

        if (s_regs.cmd.target_written)
        {
            s_target_c = (float)s_regs.cmd.target_c10 / 10.0f;
            s_regs.cmd.target_written = false;
        }

        /* Only the fault loop passes a fault, and the policy refuses a clear
         * without one; checked again here because this path restarts the
         * heater's normal boot. */
        bool do_clear = s_regs.cmd.clear_approved && resp_len > 0 &&
                        active_fault != ERROR_NONE && s_clears_left > 0U;
        s_regs.cmd.clear_approved = false;

        if (do_clear)
        {
            /* Spec order: decrement, clear the flash, reply, reset. The reply
             * waits for the erase so it can say whether it worked. The clear
             * is spent either way (fail closed). */
            s_clears_left--; /* volatile .noinit: in RAM before any reset */
            ClearErrorCode();

            if (LoadErrorCode() == ERROR_NONE)
            {
                s_last_clear_result = CLEAR_OK;
                s_regs.snap.last_clear_result = CLEAR_OK;
                link_uart_send(resp, (size_t)resp_len);
                s_last_frame_tick = now;
                /* link_uart_send returns only after TC, so the reply is fully
                 * on the wire. DSB first, so the stores are in RAM; does not return. */
                NVIC_SystemReset();
            }

            /* The fault page still holds the fault: report a device failure
             * for the same function code, and stay in the fault loop. There is
             * no ClearResult for this, so the register falls back to "none"
             * rather than claim a clear that did not happen. */
            s_last_clear_result = CLEAR_NONE;
            s_regs.snap.last_clear_result = CLEAR_NONE;
            resp_len = modbus_exception(req[1], MODBUS_EX_SERVER_FAILURE, resp, sizeof resp);
        }

        if (resp_len > 0)
        {
            link_uart_send(resp, (size_t)resp_len);
            s_last_frame_tick = now;
        }
    }

    if (link_state_tick(&s_link, now) == LINK_EVT_HOLD_EXPIRED)
    {
        s_target_c = 0.0f; /* not a fault: heating restarts only on a new target */
    }
}

float LinkApp_AirTarget(void)
{
    return s_target_c;
}

bool LinkApp_InHold(void)
{
    return s_link.in_hold;
}

bool LinkApp_LinkUp(void)
{
    return s_link.seen_activity && !s_link.expired;
}

uint32_t LinkApp_LastFrameTick(void)
{
    return s_last_frame_tick;
}

#endif /* INPUT_MODE == INPUT_MODE_UART */
