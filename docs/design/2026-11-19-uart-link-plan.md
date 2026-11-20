# Two-way UART link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new `iheater_rev1_1_uart` build where iHeater answers Modbus RTU on USART1 (USB-C D+/D−), reports full state, takes its chamber target, holds through short link outages, and lets allowlisted faults be cleared remotely when its own readings are safe.

**Architecture:** All protocol and policy logic lives in HAL-free C libraries under `Lib/` (Modbus framing, link hold timer, fault policy, register map) and is unit-tested on the PC with PlatformIO's `native` env and Unity. A thin hardware layer (`link_uart.c`, register-level USART1) and a glue module (`link_app.c`) connect them to `main.c` under `#if INPUT_MODE == INPUT_MODE_UART`. Existing builds keep their behaviour.

**Tech Stack:** C (gnu11), STM32Cube HAL + CMSIS registers on STM32F042F6, PlatformIO (`ststm32`, `native`), Unity, Python 3 + pymodbus for the hardware probe.

**Spec:** [2026-11-19-uart-link-design.md](2026-11-19-uart-link-design.md)

## Global Constraints

- Existing envs (`iheater_rev1_1_pulse`, `iheater_rev1_0`, `iheater_rev1_1`, `idryer_rev1_1`) must build after every task, with no behaviour change.
- USART1, 115200 8N1, PA9 TX / PA10 RX, AF1. `SYSCFG_CFGR1.PA11_PA12_RMP` stays clear.
- Modbus slave address 1. Frames for other addresses, or with bad CRC, get no reply and are not link activity.
- Temperatures on the wire: signed 16-bit tenths of °C; unreadable → `0x8000`.
- Link hold after 3000 ms of silence; hold window 60 s; expiry sets target 0, not a fault.
- Remote clears: 3 per power cycle; readings safe for 5000 ms; margin 10 °C below `MAX_AIR_TEMP` and `MAX_TEMP`.
- The UART image must end below `0x08007800` (NVS page at `0x08007800`, fault page at `0x08007C00`).
- Register addresses in the spec are final; do not renumber.
- Read `MAX_TEMP`, `MAX_AIR_TEMP` etc. from `config.h` macros in code and tests. Never hard-code them: the working tree has a local `MAX_TEMP 100` edit that is not committed and must not be committed by this work.
- Code comments and docs in English (Canadian spelling).

## Review Focus

1. **A thermistor that reads garbage (NaN, ±inf, open or shorted) while a clear is requested.** Expect: not safe, clear refused with result 3. Test in Task 3.
2. **Noise bytes or a partial frame from the C3 while it reboots.** Expect: ignored with no reply; the next clean poll is answered. Test in Task 2.
3. **`HAL_GetTick()` wrapping past `0xFFFFFFFF` during a hold.** Expect: hold start and expiry still happen at the right times. Test in Task 4.
4. **A write-multiple (FC 0x10) whose second register is refused.** Expect: the first write stays applied, the reply is the exception, nothing after it is applied. Test in Task 2.
5. **A read request big enough that its reply would overflow the reply buffer.** Expect: exception 0x03, no overflow. Test in Task 2.

---

## File Structure

| File | Responsibility |
|---|---|
| `platformio.ini` | new `native` (tests) and `iheater_rev1_1_uart` envs |
| `Core/Inc/config.h` | `INPUT_MODE_UART`, `FAULT_TABLE`, link constants |
| `Lib/ModbusRtu/modbus_rtu.{h,c}` | CRC16 and request→reply for FC 03/04/06/10 |
| `Lib/LinkCore/link_state.{h,c}` | silence → hold → expiry timer |
| `Lib/LinkCore/fault_policy.{h,c}` | allowlist mask, safe-readings tracker, clear decision |
| `Lib/LinkCore/link_regs.{h,c}` | register map: snapshot → registers, writes → commands |
| `Core/Inc/link_uart.h`, `Core/Src/link_uart.c` | USART1 setup, RX interrupt, end-of-frame by receiver timeout, blocking TX |
| `Core/Inc/link_app.h`, `Core/Src/link_app.c` | glue: snapshot from `main.c` globals, frame handling, hold, clear and reset, clears counter |
| `Core/Src/main.c` | UART-build hooks, shared temperature update, fault-loop servicing, LEDs |
| `Core/Src/stm32f0xx_it.c` | `USART1_IRQHandler` |
| `Core/Src/stm32f0xx_hal_msp.c` | TH2 back to analog in the UART build |
| `ld/STM32F042F6_iheater_link.ld` | linker script for the UART build: 30 KB flash, `.noinit` RAM |
| `test/test_modbus/test_main.c`, `test/test_link_state/test_main.c`, `test/test_fault_policy/test_main.c`, `test/test_link_regs/test_main.c` | Unity tests |
| `tools/link_probe.py` | PC Modbus probe for hardware checks |
| `docs/*/README.md`, `CHANGELOG.md` | user docs |

---

### Task 1: Host test harness and UART config constants

**Files:**
- Modify: `platformio.ini`
- Modify: `Core/Inc/config.h` (input-mode block near line 90; add link section)
- Create: `test/test_harness/test_main.c`

**Interfaces:**
- Produces in `config.h`, always defined (not only in the UART build):
  `INPUT_MODE_UART 2`, `LINK_SILENCE_MS 3000U`, `LINK_HOLD_S 60U`, `LINK_MIN_TARGET 45U`, `LINK_MAX_TARGET (MAX_AIR_TEMP - 5)`, `LINK_CLEAR_MARGIN 10.0f`, `LINK_CLEAR_SAFE_MS 5000U`, `LINK_REMOTE_CLEARS 3U`, `LINK_BOOTLOADER_MAGIC 0xB007U`, `LINK_RX_BUF 64U`, `LINK_TX_BUF 64U`.
  `FAULT_TABLE(X)` as X-macro rows `X(code, remote_clear)`, in this order and with these values:
  `ERROR_FAULT_TEMP_TIMEOUT 1`, `ERROR_FAULT_HEATER_NORESP 0`, `ERROR_MANUAL_SHUTDOWN 1`, `ERROR_THERMISTOR_OPEN_AIR 1`, `ERROR_THERMISTOR_OPEN_HEATER 1`, `ERROR_OVERHEAT_HEATER 0`, `ERROR_OVERHEAT_AIR 1`, `ERROR_LINK_LOST 1`, `ERROR_UNKNOWN 0`.
- Produces env `native` for `pio test -e native`.

- [ ] **Step 1: Host compiler.** `gcc --version` must work in Git Bash. It does not on this machine today. Ask the user before installing anything; suggest `scoop install mingw` (the user has scoop). Expected: `gcc (…) 1x.x` printed.

- [ ] **Step 2: Write the harness test** `test/test_harness/test_main.c` with Unity. It includes `config.h`, asserts `INPUT_MODE_UART == 2`, `LINK_MAX_TARGET == MAX_AIR_TEMP - 5`, and that counting `FAULT_TABLE` rows gives 9.

- [ ] **Step 3: Run it to verify it fails.** Run: `pio test -e native -f test_harness`. Expected: fails, because env `native` does not exist yet.

- [ ] **Step 4: Add `[env:native]`.** The common `[env]` section sets the stm32cube framework, the board and the bin-conversion script, so override all three:
  `platform = native`, `framework =`, `board =`, `extra_scripts =`, `test_framework = unity`, `build_flags = -std=gnu11 -DINPUT_MODE=INPUT_MODE_UART`, `debug_init_cmds =`. Leave `default_envs` unchanged so `pio run` never builds `native`. Then add the `config.h` constants and `FAULT_TABLE` above. Update the input-mode comment block to describe the third mode: "USART1 on USB-C; TH2 is an aux thermistor".

- [ ] **Step 5: Run.** `pio test -e native -f test_harness` → PASS. `pio run` (all four default envs) → SUCCESS.

- [ ] **Step 6: Commit.** `git add platformio.ini Core/Inc/config.h test/test_harness` (not the `MAX_TEMP` hunk: use `git add -p Core/Inc/config.h`). Message: `build: native test env and UART link constants`.

---

### Task 2: Modbus RTU core

**Files:**
- Create: `Lib/ModbusRtu/modbus_rtu.h`, `Lib/ModbusRtu/modbus_rtu.c`
- Test: `test/test_modbus/test_main.c`

**Interfaces:**
- Produces:
```c
#define MODBUS_SLAVE_ADDR           1U
#define MODBUS_EX_ILLEGAL_FUNCTION  0x01U
#define MODBUS_EX_ILLEGAL_ADDRESS   0x02U
#define MODBUS_EX_ILLEGAL_VALUE     0x03U
typedef struct {
    uint8_t (*read_input)(void *ctx, uint16_t addr, uint16_t *out);   /* 0 = ok, else exception */
    uint8_t (*read_holding)(void *ctx, uint16_t addr, uint16_t *out);
    uint8_t (*write_holding)(void *ctx, uint16_t addr, uint16_t value);
    void *ctx;
} ModbusHandlers;
uint16_t modbus_crc16(const uint8_t *data, size_t len);
/* -1: ignore (too short, bad CRC, other address); else reply length written to resp. */
int modbus_handle_frame(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t resp_cap, const ModbusHandlers *h);
```

- [ ] **Step 1: Write failing tests** in `test/test_modbus/test_main.c`, using fake handlers backed by a 0x300-entry array (`fake[addr] = addr * 3`, writes recorded, address `0x0200` refused with 0x02, value `0xDEAD` refused with 0x03):
  - `test_crc_known_vector`: CRC of `01 03 00 00 00 0A` = `0xCDC5` (low byte first on the wire: `C5 CD`).
  - `test_fc04_reads_two`: request `01 04 00 09 00 02`+CRC → reply `01 04 04 00 1B 00 1E`+CRC, length 9.
  - `test_fc03_reads_one`: `01 03 01 00 00 01` → `01 03 02 03 00`+CRC.
  - `test_fc06_echoes`: `01 06 01 00 02 26`+CRC → reply equals request; fake recorded `[0x0100]=0x0226`.
  - `test_fc10_writes_two`: write `0x0100..0x0101` = `{1, 2}` → reply `01 10 01 00 00 02`+CRC.
  - `test_fc10_stops_at_refused` (Review Focus 4): write `0x01FF..0x0200` = `{7, 8}` → `[0x01FF]=7` applied, reply `01 90 02`+CRC.
  - `test_unknown_function`: FC `0x2B` → `01 AB 01`+CRC.
  - `test_handler_exception`: FC04 at `0x0200` → `01 84 02`+CRC.
  - `test_quantity_zero_or_over_125`: FC04 quantity 0 and 126 → exception 0x03.
  - `test_reply_would_overflow` (Review Focus 5): `resp_cap = 16`, FC04 quantity 10 → exception 0x03, nothing written past `resp[4]`.
  - `test_other_address_ignored`: address 2 → returns -1. Address 0 (broadcast) → -1, no handler called.
  - `test_bad_crc_ignored`: last byte flipped → -1, no handler called.
  - `test_leading_noise_ignored` (Review Focus 2): `FF` + a valid FC04 frame as one buffer → -1; the same valid frame alone → answered.
  - `test_short_frame_ignored`: lengths 0–3 → -1.

- [ ] **Step 2: Run to verify fail.** `pio test -e native -f test_modbus` → fails to compile (no `modbus_rtu.h`).

- [ ] **Step 3: Implement** `modbus_crc16` (poly `0xA001` reflected, init `0xFFFF`) and `modbus_handle_frame` in `Lib/ModbusRtu/modbus_rtu.c`. Exception reply: `addr, fc|0x80, code, crc`. FC 0x10 checks byte count = 2 × quantity, else 0x03. No dynamic memory, no HAL includes.

- [ ] **Step 4: Run.** `pio test -e native -f test_modbus` → all PASS.

- [ ] **Step 5: Commit.** `feat(link): Modbus RTU frame handling`

---

### Task 3: Fault allowlist and clear policy

**Files:**
- Create: `Lib/LinkCore/fault_policy.h`, `Lib/LinkCore/fault_policy.c`
- Test: `test/test_fault_policy/test_main.c`

**Interfaces:**
- Consumes: `FAULT_TABLE`, `LINK_*` constants, `MIN_AIR_TEMP`, `MAX_AIR_TEMP`, `MIN_TEMP`, `MAX_TEMP` from `config.h`.
- Produces:
```c
typedef enum {
    CLEAR_NONE = 0, CLEAR_OK = 1, CLEAR_NOT_ALLOWED = 2, CLEAR_UNSAFE = 3,
    CLEAR_NO_CLEARS_LEFT = 4, CLEAR_CODE_MISMATCH = 5, CLEAR_NO_FAULT = 6
} ClearResult;
typedef struct { bool safe; uint32_t safe_since_ms; } SafeTracker;
uint16_t    fault_clear_allow_mask(void);           /* bit n = code n remote-clearable; codes > 15 never */
bool        fault_remote_clearable(uint8_t code);
bool        fault_readings_safe(float air_c, float heater_c);
void        safe_tracker_init(SafeTracker *t);
void        safe_tracker_update(SafeTracker *t, float air_c, float heater_c, uint32_t now_ms);
bool        safe_tracker_ok(const SafeTracker *t, uint32_t now_ms);  /* safe continuously >= LINK_CLEAR_SAFE_MS */
ClearResult fault_evaluate_clear(uint8_t active_fault, uint16_t requested, uint8_t clears_left, bool safe_ok);
```

- [ ] **Step 1: Write failing tests:**
  - `test_allow_mask`: equals `0x01BA` (codes 1, 3, 4, 5, 7, 8).
  - `test_clearable_lookup`: 0x02, 0x06, 0xFF false; 0x07 true; 0x00 false.
  - `test_safe_boundaries`: air = `MAX_AIR_TEMP - LINK_CLEAR_MARGIN` with heater 40 → safe. Air 0.1 above that → not safe. Same at the heater edge with `MAX_TEMP`. Air `MIN_AIR_TEMP` exactly → not safe (open thermistor reads low).
  - `test_garbage_readings_unsafe` (Review Focus 1): `NAN`, `INFINITY`, `-INFINITY` in either argument → not safe.
  - `test_tracker_needs_5s`: safe at t=1000 → `ok` false at 5999, true at 6000. One unsafe update at 3000 restarts the clock (true only from 8000 after safe again at 3000+).
  - `test_tracker_wraps`: safe from `0xFFFFF000`, `ok` true at `0xFFFFF000 + 5000` (wrapped).
  - `test_evaluate_order`: no fault → 6; requested ≠ active → 5; active 0x02 requested 0x02 → 2; clears_left 0 → 4; not safe → 3; all good → 1. Check that each earlier condition wins over later ones (for example a mismatch with clears_left 0 → 5).

- [ ] **Step 2: Run to verify fail.** `pio test -e native -f test_fault_policy` → compile error.

- [ ] **Step 3: Implement** in `Lib/LinkCore/fault_policy.c`. Build the mask from `FAULT_TABLE`. Use `isfinite()` for the garbage check. Use unsigned subtraction for time.

- [ ] **Step 4: Run.** → PASS.

- [ ] **Step 5: Commit.** `feat(link): fault allowlist and remote clear policy`

---

### Task 4: Link hold timer

**Files:**
- Create: `Lib/LinkCore/link_state.h`, `Lib/LinkCore/link_state.c`
- Test: `test/test_link_state/test_main.c`

**Interfaces:**
- Produces:
```c
typedef enum { LINK_EVT_NONE = 0, LINK_EVT_HOLD_STARTED, LINK_EVT_HOLD_EXPIRED } LinkEvent;
typedef struct {
    uint32_t last_activity_ms, hold_start_ms;
    uint16_t loss_count;
    bool seen_activity, in_hold, expired;
} LinkState;
void      link_state_init(LinkState *s);
void      link_state_activity(LinkState *s, uint32_t now_ms);   /* ends hold or expiry */
LinkEvent link_state_tick(LinkState *s, uint32_t now_ms);       /* each event returned once */
uint16_t  link_state_hold_remaining_s(const LinkState *s, uint32_t now_ms); /* 0 unless in_hold; rounds up */
```

- [ ] **Step 1: Write failing tests:**
  - `test_no_hold_before_first_activity`: init, tick at 100000 → NONE, `loss_count` 0.
  - `test_hold_starts_at_3000`: activity at 1000; tick at 3999 → NONE; at 4000 → HOLD_STARTED; at 4001 → NONE; `loss_count` 1.
  - `test_remaining_counts_down`: hold started at 4000 → remaining 60 at 4000, 60 at 4001, 1 at 63999, then tick at 64000 → HOLD_EXPIRED, remaining 0.
  - `test_expired_reported_once`: ticks at 64001 and 200000 → NONE; no second hold until new activity.
  - `test_activity_ends_hold`: hold at 4000, activity at 10000 → `in_hold` false, remaining 0; silence again → a second HOLD_STARTED at 13000, `loss_count` 2.
  - `test_wraparound` (Review Focus 3): activity at `0xFFFFF000`; HOLD_STARTED at `0xFFFFF000 + 3000` (wrapped); HOLD_EXPIRED 60000 ms after that.

- [ ] **Step 2: Run to verify fail.** `pio test -e native -f test_link_state` → compile error.

- [ ] **Step 3: Implement** in `Lib/LinkCore/link_state.c` with `LINK_SILENCE_MS` and `LINK_HOLD_S`.

- [ ] **Step 4: Run.** → PASS.

- [ ] **Step 5: Commit.** `feat(link): link hold timer`

---

### Task 5: Register map

**Files:**
- Create: `Lib/LinkCore/link_regs.h`, `Lib/LinkCore/link_regs.c`
- Test: `test/test_link_regs/test_main.c`

**Interfaces:**
- Consumes: `fault_clear_allow_mask`, `fault_evaluate_clear`, `ClearResult` (Task 3); `config.h` constants; `VERSION_MAJOR/MINOR/PATCH` from `version.h`.
- Produces:
```c
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
int16_t link_temp_c10(float c);                      /* round to nearest; !isfinite → INT16_MIN (0x8000) */
uint8_t link_reset_cause(uint32_t rcc_csr);          /* priority: power-on > watchdog > software > pin */
uint8_t link_regs_read_input(void *ctx, uint16_t addr, uint16_t *out);
uint8_t link_regs_read_holding(void *ctx, uint16_t addr, uint16_t *out);
uint8_t link_regs_write_holding(void *ctx, uint16_t addr, uint16_t value);
```
  Reset-cause bit masks are defined locally in `link_regs.c` (not CMSIS): `PORRSTF 1u<<27`, `PINRSTF 1u<<26`, `SFTRSTF 1u<<28`, `IWDGRSTF 1u<<29`.
  `ctx` is a `LinkRegs *`. All three handler signatures match `ModbusHandlers` (Task 2).

- [ ] **Step 1: Write failing tests:**
  - `test_input_map`: fill a snapshot with distinct values. Assert every input address 0x00–0x15 per the spec table, including `0x01 = VERSION_MAJOR<<8 | VERSION_MINOR`, `0x06 = 0x01BA`, `0x0E = pwm_pct*10` (42.5 % → 425), `0x10/0x11` as the low/high words of `uptime_s = 0x00012345`, and `0x0B = 0x8000` when `aux_valid` is false. Address 0x16 → 0x02.
  - `test_temp_c10`: 54.24 → 542, 54.25 → 543 (round half away from zero), −3.25 → −33, `NAN` → `INT16_MIN`.
  - `test_settings_block`: 0x0200..0x0209 read `MAX_TEMP*10`, `MAX_AIR_TEMP*10`, `LINK_MIN_TARGET*10`, `LINK_MAX_TARGET*10`, `LINK_HOLD_S`, `DELTA*10`, `FAN_ON_TEMP*10`, `FAN_OFF_TEMP*10`, `FAN_ON_ACTIVE_MODE_TEMP*10`, `LINK_CLEAR_MARGIN*10`. Any write there → 0x02. 0x020A → 0x02.
  - `test_target_write`: 0 accepted; `LINK_MIN_TARGET*10` and `LINK_MAX_TARGET*10` accepted; one below min (but > 0) and one above max → 0x03 and `cmd.target_written` stays false. On accept, `cmd.target_c10` holds the value and a read of 0x0100 returns it.
  - `test_target_refused_in_fault`: `active_fault = 0x07` → write 0x0100 → 0x02.
  - `test_clear_write`: active 0x07, clears_left 3, safe_ok → write 0x0101 = 7 → 0, `cmd.clear_approved`, `last_clear_result = 1`. With safe_ok false → 0x03, result 3, not approved. 0x0101 reads 0.
  - `test_bootloader_reserved`: write 0x0102 = `0xB007` → 0x02; read 0x0102 → 0.
  - `test_reset_cause`: `PORRSTF|PINRSTF` → POWER_ON; `IWDGRSTF|PINRSTF` → WATCHDOG; `SFTRSTF` → SOFTWARE; `PINRSTF` → PIN; 0 → UNKNOWN.

- [ ] **Step 2: Run to verify fail.** `pio test -e native -f test_link_regs` → compile error.

- [ ] **Step 3: Implement** `Lib/LinkCore/link_regs.c`.

- [ ] **Step 4: Run all host tests.** `pio test -e native` → all suites PASS.

- [ ] **Step 5: Commit.** `feat(link): register map`

---

### Task 6: UART build: driver, glue, normal-loop integration

**Files:**
- Create: `ld/STM32F042F6_iheater_link.ld`
- Create: `Core/Inc/link_uart.h`, `Core/Src/link_uart.c`
- Create: `Core/Inc/link_app.h`, `Core/Src/link_app.c`
- Create: `tools/link_probe.py`
- Modify: `platformio.ini` (new env), `Core/Src/main.c`, `Core/Src/stm32f0xx_it.c`, `Core/Src/stm32f0xx_hal_msp.c`, `Core/Inc/config.h` (`MODE_PULSE` comment)

**Interfaces:**
- Consumes: Tasks 2–5.
- Produces:
```c
/* link_uart.h */
void link_uart_init(void);                                  /* USART1 115200 8N1, PA9/PA10 AF1, RX IRQ, RTO */
bool link_uart_take_frame(uint8_t *buf, size_t *len);       /* true once per completed frame; copies it out */
void link_uart_send(const uint8_t *buf, size_t len);        /* blocking, waits for TC */
void link_uart_irq(void);                                   /* called from USART1_IRQHandler */
/* link_app.h */
void  LinkApp_Init(uint8_t active_fault);   /* reset cause, clears counter, safe tracker, link state, uart */
void  LinkApp_Service(uint8_t active_fault);/* every loop pass, main loop and fault loop */
float LinkApp_AirTarget(void);              /* commanded target °C, 0 = off */
bool  LinkApp_InHold(void);
uint32_t LinkApp_LastFrameTick(void);       /* for the LED3 frame flash */
```

- [ ] **Step 1: Linker script.** Copy `~/.platformio/packages/tool-ldscripts-ststm32/stm32f0/STM32F042F6PX_FLASH.ld` to `ld/STM32F042F6_iheater_link.ld`. Change `FLASH LENGTH = 32K` to `30K` (NVS and fault pages reserved). Add after `.bss`:
  `.noinit (NOLOAD) : { . = ALIGN(4); *(.noinit) *(.noinit*) . = ALIGN(4); } >RAM`.

- [ ] **Step 2: Env.** `[env:iheater_rev1_1_uart]` with build flags `-O0 -ggdb -DDEVICE_PROFILE=DEVICE_PROFILE_HEATER -DBOARD_REVISION=BOARD_REV_1_1 -DINPUT_MODE=INPUT_MODE_UART` and `board_build.ldscript = ld/STM32F042F6_iheater_link.ld`. Add it to `default_envs`.

- [ ] **Step 3: Driver** `link_uart.c`, register-level (no HAL UART; it costs too much flash): enable `RCC->APB2ENR USART1EN`. PA9/PA10 AF1, push-pull, pull-up on RX. `BRR = 417` (48 MHz / 115200). `CR2.RTOEN`, `RTOR = 202` bit times (≈1.75 ms). `CR1: RE TE RXNEIE RTOIE UE`. The IRQ stores bytes into a `LINK_RX_BUF` buffer. On overflow it drops the frame until the next RTO. On RTO it marks the frame ready (ignoring empty frames) and clears `ICR.RTOCF`; it also clears `ORE`. `USART1_IRQHandler` in `stm32f0xx_it.c` under `#if INPUT_MODE == INPUT_MODE_UART` calls `link_uart_irq()`. NVIC priority 2, the same as the pulse EXTI.

- [ ] **Step 4: Glue** `link_app.c`. It reads `main.c` globals through `extern`: `air_temp`, `heater_temp`, `temperatures[]`, `heater_setpoint`, `pwm`, and the FAN pin state.
  - `LinkApp_Service` steps, in order:
    1. `safe_tracker_update`.
    2. Take a frame.
    3. If the frame's return is ≥ 0, call `link_state_activity`.
    4. Fill `LinkRegs.snap` (state: fault if `active_fault`, else hold if in hold, else heating if target > 0, else idle; aux from `temperatures[2]`, valid when finite).
    5. `modbus_handle_frame` with the `link_regs_*` handlers.
    6. Send the reply.
    7. Apply `cmd.target_written` (store °C).
    8. If `cmd.clear_approved` — handled in Task 7; in this task, leave a stub that does nothing.
    9. `link_state_tick`; on `LINK_EVT_HOLD_EXPIRED` set the target to 0.
  - Keep `clears_left` for now as a plain static initialised to `LINK_REMOTE_CLEARS` (Task 7 moves it).
  - `reset_cause`: read `RCC->CSR` once in `LinkApp_Init`, map it with `link_reset_cause`, then clear the flags (`RCC->CSR |= RCC_CSR_RMVF`).

- [ ] **Step 5: main.c hooks**, all under `#if INPUT_MODE == INPUT_MODE_UART`:
  - Call `LinkApp_Init(ERROR_NONE)` after the ADC DMA start.
  - In the loop, where the pulse build handles frames, call `LinkApp_Service(ERROR_NONE)`, then `mode = LinkApp_AirTarget() > 0 ? MODE_PULSE : MODE_0`.
  - Where the pulse build sets `air_target`, use `air_target = LinkApp_AirTarget()`.
  - ADC: the UART build uses 3 channels (TH2 back on `ADC_CHANNEL_9`), so change `ADC_CHANNEL_COUNT`, `MX_ADC_Init`, and `HAL_ADC_MspInit` conditions so the pulse build alone excludes TH2.
  - Exclude the trigger block and the runtime MODE-button block in the UART build, the same way the pulse build does.
  - Write a `LINK_BUILD` helper macro (`INPUT_MODE != INPUT_MODE_ANALOG`) only where both link builds share a condition.
  - Rename the `MODE_PULSE` comment to "link-controlled heating (pulse or UART build)".

- [ ] **Step 6: LEDs** in `Update_LEDs` for the UART build: reuse the pulse patterns.
  - Waiting: no frame since boot.
  - Idle: LED1 solid, plus an 80 ms LED3 flash per answered frame from `LinkApp_LastFrameTick()`.
  - Heating: chase at 500 ms.
  - Hold: all three LEDs blink together at 4 Hz (toggle every 125 ms).

- [ ] **Step 7: Probe** `tools/link_probe.py` (pymodbus 3.x, pyserial). Usage: `python tools/link_probe.py COM5 read`, `… target 60`, `… clear 7`, `… raw`. `read` prints all input registers 0x00–0x15 and holding 0x0100–0x0102, 0x0200–0x0209, decoding temperatures and enums. Exit code 1 on a timeout or exception.

- [ ] **Step 8: Build.** `pio run` → all five envs SUCCESS. The UART env's link must succeed, which proves the image fits in 30 KB. Record the flash percentage in the commit message. If the UART env overflows, change only its `-O0` to `-Os` and note it in the commit.

- [ ] **Step 9: Hardware check (user).** Flash the UART build. Connect a 3.3 V USB-serial adapter to D− (iHeater TX) and D+ (iHeater RX) and GND, with 5V from the adapter or the C3, not both. Expected:
  - `read` shows plausible air and heater temperatures, `state` = 1, `proto_version` = 1.
  - `target 60` → `state` = 2 and the PWM rises.
  - Stop polling for more than 3 s → hold LED pattern, then after 60 s → `state` = 1 and the target is 0.
  - Polling `read` every 200 ms keeps `link_loss_count` steady.

- [ ] **Step 10: Commit.** `feat(link): UART build with Modbus link (normal operation)`

---

### Task 7: Fault loop servicing and remote clear

**Files:**
- Modify: `Core/Src/main.c` (temperature update extraction, boot fault loop at ~275–296), `Core/Src/link_app.c`

**Interfaces:**
- Consumes: Task 6 glue, `ClearErrorCode()` and `NVIC_SystemReset()` from `main.c`.
- Produces in `main.c` (all builds): `static void UpdateTemperatures(void)`. This is the ADC averaging, EMA and thermistor block now inline at the top of the loop (lines ~367–386), including the `heater_temp = temperatures[1]; air_temp = temperatures[0];` assignments. It moves unchanged, and the loop calls it in the same place.

- [ ] **Step 1: Extract `UpdateTemperatures()`.** Pure move. `pio run` → all envs SUCCESS. Commit `refactor: share temperature update`.

- [ ] **Step 2: Clears counter in `.noinit`.** In `link_app.c`: `static uint32_t s_clears_magic __attribute__((section(".noinit")));` plus `s_clears_left` the same way. In `LinkApp_Init`, if the reset cause is POWER_ON or `s_clears_magic != 0x434C5253`, set the magic and `s_clears_left = LINK_REMOTE_CLEARS`.

- [ ] **Step 3: Clear handling.** In `LinkApp_Service`, after sending the reply for an approved clear:
  1. `s_clears_left--`
  2. `ClearErrorCode()`
  3. wait for TX complete
  4. `NVIC_SystemReset()`

  `active_fault` is passed in by the caller, so `LinkApp_Service(code)` is used in the fault loop and `LinkApp_Service(ERROR_NONE)` in the main loop.

- [ ] **Step 4: Fault loop.** Under `#if INPUT_MODE == INPUT_MODE_UART`, the boot fault loop calls `LinkApp_Init(code)` before entering. It keeps the heater at 0 (`SetHeaterPWM(0.0f)`) and the fan on. In each pass it calls `UpdateTemperatures()`, `LinkApp_Service(code)`, `Update_LEDs(code, …)` and refreshes the watchdog. MODE at power-on still clears locally, exactly as today. `last_fault` in the snapshot comes from the loaded code at boot. Keep it in a `.noinit` variable next to the counter so it survives the clear reset; reset it to 0 on power-on.

- [ ] **Step 5: Build.** `pio run` → all envs SUCCESS.

- [ ] **Step 6: Hardware check (user).**
  - Force air over-temperature by temporarily setting `// code = 0x07;` at main.c:276 in a local build only, never committed. Expected: `read` shows `state` = 4, `active_fault` = 7, live temperatures updating, `fault_clearable_now` = 1 after 5 s.
  - `clear 7` → board resets into normal state; `remote_clears_left` = 2; `last_fault` = 7.
  - Repeat until `remote_clears_left` = 0 → `clear` refused with result 4.
  - Power-cycle → back to 3.
  - Force 0x06 → `clear 6` refused with result 2.
  - `clear 5` while 0x07 is active → result 5.

- [ ] **Step 7: Commit.** `feat(link): report and clear faults over the link`

---

### Task 8: Docs and changelog

**Files:**
- Modify: `docs/en/README.md` (after the "Working with iHeater-link (r1.1 pulse build)" section), `docs/*/README.md` for every other language folder, `CHANGELOG.md`, `README.md` if it lists builds.

- [ ] **Step 1: English section** "Working with iHeater Remote (UART build)":
  - cable pinout table: VBUS 5V, GND, D− = iHeater TX, D+ = iHeater RX
  - the 5V-JST warning
  - serial settings
  - hold behaviour (3 s, 60 s)
  - the fault table with the remote-clear column and the clear rule
  - LED table
  - a link to the design doc for the full register map
  - `tools/link_probe.py` usage

- [ ] **Step 2: Other languages.** Add the same section to each `docs/<lang>/README.md`. Translate it if you can do so accurately. Otherwise use the English text under a one-line "translation needed" note in that language's heading style.

- [ ] **Step 3: CHANGELOG** entry under Unreleased: new `iheater_rev1_1_uart` build.

- [ ] **Step 4: Verify.** `pio run` → SUCCESS; `pio test -e native` → PASS.

- [ ] **Step 5: Commit.** `docs: UART link build`
