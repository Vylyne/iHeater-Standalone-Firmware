# Two-way UART link to iHeater Remote — design

Status: approved · 2026-11-19

## Intent

Replace the one-way pulse line (TH2 / PB1) between iHeater and the
ESP32-C3 iHeater Remote with a two-way serial link on the USB-C connector,
so the Remote and Klipper (`heater_virtual`) see the iHeater's real state in
near real time, and so faults can be managed from the Remote when the
iHeater itself judges it safe.

Why: today the Remote only sends a chamber target. Klipper's chamber
temperature comes from a separate sensor, its `power` is a guess (1.0 when a
target is set), and any iHeater fault needs a trip to the printer and a MODE
press at power-on.

## Goals

1. A new build, `iheater_rev1_1_uart`, that talks Modbus RTU over USART1 on
   the USB-C D+/D− lines. The pulse build stays as it is.
2. All iHeater state is readable over the link at all times, including while
   a fault is latched: temperatures, targets, PWM, fan, state, active fault,
   link statistics, firmware version and build limits.
3. The Remote sets the chamber target over the link.
4. Faults can be cleared over the link, but only faults on an allowlist, and
   only when the iHeater's own live readings say it is safe.
5. The iHeater's limits (`MAX_TEMP`, `MAX_AIR_TEMP`, target range, link hold
   time, …) are readable now and laid out so they can become writable later
   without changing the protocol.
6. A short link outage (for example the C3 rebooting) does not drop heat.

## Non-goals

- Writing settings over the link (v1 reports them read-only; see "Settings").
- Remote and Klipper changes. That is the second project: the Remote's
  Modbus client, reporting into Klipper, and fault UI. This document fixes
  the contract they build against.
- Reflashing the STM32 over the link. v1 only reserves the
  `enter_bootloader` register for it (see the register map). The ROM
  bootloader's USART1 mode uses PA9/PA10, which are already on pins 17/18
  with no remap; the rest is not investigated.
- Board revisions other than r1.1.

## Anti-goals

- A remote action that can restart heating after a fault that may mean the
  heater thermistor has come off the element (heater-no-response,
  heater over-temperature).
- Heat continuing on a stale target for longer than the hold window when
  nobody is talking to the iHeater.
- The iHeater going silent exactly when it is in a fault state.
- Any change in behaviour for existing pulse or analog builds.

## Constraints

- **MCU:** STM32F042F6, TSSOP20. 32 KB flash, 6 KB RAM. The pulse build is
  25.7 KB at `-O0` (78 %). The last two flash pages are in use but not
  reserved in the linker script: `0x08007800` holds the trigger calibration
  (NVS) and `0x08007C00` the persisted fault code. The image must stay below
  30 KB (`0x08007800`); the UART build's linker script reserves both pages.
- **Pins:** USB-C D− → pin 17, D+ → pin 18, through 22–40 Ω series
  resistors and an ESD array (measured by the user). With
  `SYSCFG_CFGR1.PA11_PA12_RMP` clear (the reset default), pins 17/18 are
  PA9/PA10: USART1_TX on D−, USART1_RX on D+ (AF1). Setting the bit puts
  PA11/PA12 (USB) there instead, so the UART build must leave it clear
  (confirmed against the HAL definition of `HAL_REMAP_PA11_PA12`). USART1
  also supports TX/RX swap (`CR2.SWAP`) if wiring comes out reversed.
- **Power:** the iHeater has no regulator; its logic runs from 5V supplied
  by the C3 over the same cable (VBUS). A C3 software reset does not cut
  power to the iHeater. The 5V JST must not be connected at the same time.
- **Cable:** short custom cable, 5V, GND, and two signal wires to D+/D−.
- **Remote side (for project 2):** ESP32-C3 Super Mini. Use UART1 on free
  GPIOs, avoiding GPIO20/21 (boot console) and strapping pins 2/8/9.
  The existing pulse pin is GPIO3.
- **Watchdog:** IWDG at about 3.2 s. Every loop, including the fault loop,
  must keep refreshing it.

## Approaches

- **A. Modbus RTU register map (chosen by the user).** The ESP polls; the
  iHeater answers. Telemetry is input registers, target and settings are
  holding registers, and fault clear is a register write. Settings-by-ID is
  built in, CRC on every frame, polling gives a natural link-alive signal,
  and standard PC Modbus tools work for testing. About 1.5 KB for a minimal
  responder.
- B. Custom binary packets, iHeater pushing state. Smallest code, but
  addressing, acknowledgements, settings and versioning would be invented
  from scratch. Rejected.
- C. Text lines. Readable in a terminal, but the most flash for parsing and
  an ad-hoc grammar for settings and errors. Rejected.
- D. idryer-core's `UartBridge` protocol (already in the Remote). Its ESP
  side is written, but payloads are dryer-shaped (one temperature per unit,
  no element temperature, no fault code, settings as fragmented JSON), the
  contract is owned upstream, and the Remote is removing most idryer-core
  code because compatibility with that ecosystem is not a goal. Rejected.

## Design

### Serial and framing

- USART1, 115200 baud, 8N1, PA9 TX / PA10 RX (no remap; see Constraints).
- Modbus RTU, slave address 1. A frame with another address, or a bad CRC,
  is ignored and does not count as link activity.
- Frame end detected by line silence. Modbus asks for 1.75 ms above
  19200 baud; USART1's receiver timeout (`RTOR`) can do this in hardware.
- Function codes: 0x03 read holding, 0x04 read input, 0x06 write single
  holding, 0x10 write multiple holding. Anything else → exception 0x01.
- Exceptions: 0x01 illegal function, 0x02 illegal address (including writes
  to read-only registers), 0x03 illegal value (out-of-range target, refused
  clear).
- All temperatures are signed 16-bit, tenths of a degree C. A sensor that is
  not fitted or not readable reports `0x8000`.

### Register map (protocol version 1)

Addresses are stable once released. New registers are added at the end of a
block; a register is never reused for a different meaning.

**Input registers (0x04, read-only), from 0x0000**

| Addr | Name | Meaning |
|---|---|---|
| 0x00 | `proto_version` | 1 |
| 0x01 | `fw_version` | major << 8 \| minor |
| 0x02 | `fw_patch` | patch |
| 0x03 | `state` | 0 booting, 1 idle (target 0), 2 heating, 3 link hold, 4 fault latched |
| 0x04 | `active_fault` | fault code, 0 = none |
| 0x05 | `fault_clearable_now` | 1 if a clear of `active_fault` would succeed right now |
| 0x06 | `clear_allow_mask` | bit *n* set = fault code *n* is on the remote-clear allowlist |
| 0x07 | `remote_clears_left` | remaining remote clears this power cycle |
| 0x08 | `last_clear_result` | 0 none yet, 1 cleared, 2 not on allowlist, 3 readings not safe, 4 no clears left, 5 code mismatch, 6 no active fault |
| 0x09 | `air_temp` | air (chamber) thermistor, TH1 |
| 0x0A | `heater_temp` | element thermistor, TH0 |
| 0x0B | `aux_temp` | TH2, reported only, not used for control |
| 0x0C | `air_target` | target currently in effect (0 = off) |
| 0x0D | `heater_setpoint` | inner-loop element setpoint from the air PID |
| 0x0E | `pwm_permille` | heater output, 0–1000 |
| 0x0F | `fan_on` | 0 / 1 |
| 0x10 | `uptime_s_lo` | seconds since boot, low word |
| 0x11 | `uptime_s_hi` | high word |
| 0x12 | `link_loss_count` | link holds entered since boot |
| 0x13 | `hold_remaining_s` | seconds left in a link hold, 0 when the link is fine |
| 0x14 | `reset_cause` | from `RCC_CSR`: power-on, watchdog, software (fault), pin |
| 0x15 | `last_fault` | most recent fault code, kept after it is cleared, 0 if none since power-on |

**Holding registers (0x03 / 0x06 / 0x10)**

Control block, from 0x0100:

| Addr | Name | Meaning |
|---|---|---|
| 0x0100 | `air_target_cmd` | write: 0 = off, else `min_target`..`max_air_target` (tenths). Out of range → exception 0x03. Read: same as `air_target`. Rejected with 0x02 while a fault is latched. |
| 0x0101 | `clear_fault` | write the fault code being cleared (must equal `active_fault`). See "Faults". Reads 0. |
| 0x0102 | `enter_bootloader` | reserved for firmware updates over the link. Future meaning: write `0xB007` → heater off, reply, then jump to the ROM bootloader. v1: writes return exception 0x02. Reads 0. |

Settings block, from 0x0200. Read-only in v1: writes return exception 0x02.
Values are the compiled-in `config.h` constants.

| Addr | Name | Source |
|---|---|---|
| 0x0200 | `max_heater_temp` | `MAX_TEMP` |
| 0x0201 | `max_air_temp` | `MAX_AIR_TEMP` (air fault trip) |
| 0x0202 | `min_target` | lowest accepted non-zero target: new `LINK_MIN_TARGET`, default 45 °C like `PULSE_MIN_TARGET` |
| 0x0203 | `max_air_target` | highest accepted target |
| 0x0204 | `link_hold_s` | link hold window |
| 0x0205 | `delta` | `DELTA` |
| 0x0206 | `fan_on_temp` | `FAN_ON_TEMP` |
| 0x0207 | `fan_off_temp` | `FAN_OFF_TEMP` |
| 0x0208 | `fan_on_active_temp` | `FAN_ON_ACTIVE_MODE_TEMP` |
| 0x0209 | `clear_margin` | safety margin used by the clear rule |

### Link activity and the hold

- Any valid frame addressed to us counts as link activity.
- The Remote is expected to poll the input block at least every 250 ms.
- No activity for 3 s → **link hold**: `state` = 3, `link_loss_count`
  increments, the current target keeps being regulated, and
  `hold_remaining_s` counts down from `link_hold_s` (default 60 s).
- Activity during the hold ends it; nothing else changes.
- Hold expires → target set to 0 (heater off). This is not a fault: nothing
  is written to flash and there is no reset. Heating restarts only when a
  new target is written.
- At boot the target is 0. There is no boot grace window, because no
  traffic simply means no heat.
- During any hold the normal local protections keep running: air and
  heater over-temperature, open thermistors, heater-no-response, air
  timeout.
- Remote reboot recovery (project 2): on reconnect the Remote reads
  `air_target_cmd` and keeps writing that value until its printer source
  confirms or changes it.

### Faults

**One fault table** in `config.h` drives everything: code, name, and whether
it can be cleared over the link. Changing the allowlist is a one-line edit
there. Starting allowlist:

| Code | Fault | Remote clear |
|---|---|---|
| 0x01 | air did not reach target in time | yes |
| 0x02 | heater not responding | **no** |
| 0x03 | manual shutdown (defined, not raised by current firmware) | yes |
| 0x04 | air thermistor open | yes |
| 0x05 | heater thermistor open | yes |
| 0x06 | heater over-temperature | **no** |
| 0x07 | air over-temperature | yes |
| 0x08 | link lost (pulse build only; not raised in the UART build) | yes |
| 0xFF | unknown | **no** |

**Fault latch is unchanged:** save the code to flash, reset, and boot into
the fault loop with the heater off and the fan on. MODE at power-on still
clears any fault locally.

**The fault loop must keep working:** it keeps converting the thermistors
(same filtering as the main loop), keeps answering Modbus with full state
(`state` = 4), keeps the LEDs showing the fault, and keeps refreshing the
watchdog.

**Clear rule.** A write of code *c* to `clear_fault` succeeds only if all of
these hold. Otherwise it returns exception 0x03 and `last_clear_result`
says why.

1. *c* equals `active_fault` (you can only clear the fault you have seen).
2. *c* is on the allowlist.
3. `remote_clears_left` > 0.
4. The live readings have been safe continuously for at least 5 s: both
   thermistors read inside their valid range, the air temperature is at
   least `clear_margin` (10 °C) below `MAX_AIR_TEMP`, and the element is at
   least `clear_margin` below `MAX_TEMP`.

On success: decrement `remote_clears_left`, clear the code in flash, reply,
then reset into a normal boot.

**Remote clears per power cycle:** 3. The counter must survive the software
reset that follows a fault or a clear, but not a power cycle: keep it in
RAM marked no-init with a magic value, re-armed when `RCC_CSR` shows a
power-on reset. An RTC backup register would also work.

### Settings, later

v1 reports settings read-only at 0x0200. When writes are added: each
setting has a compile-time ceiling (the current `config.h` value) and a
floor, so a setting can be lowered over the link but never raised above
what the firmware was built with. Values persist in the same flash page as
the fault code. The page needs a small versioned record with a checksum,
not a bare word, and a bad record falls back to the compiled defaults.

### Build and pins

- New `INPUT_MODE_UART` and a new PlatformIO env, `iheater_rev1_1_uart`
  (`DEVICE_PROFILE_HEATER`, `BOARD_REV_1_1`).
- TH2 (PB1) goes back to an ADC channel and is reported as `aux_temp`; it
  plays no part in control.
- MODE button behaves as in the pulse build: power-on fault clear only.
- LEDs reuse the pulse-build patterns: waiting for first contact, idle with
  link up, heating, link hold (all three blink together at 4 Hz), fault.

### Docs

- `docs/en/README.md`: a "Working with iHeater Remote (UART build)" section
  covering the cable pinout, the register map (or a link to it), hold
  behaviour and fault clearing.
- The other languages under `docs/` get the same section, translated or
  marked as needing translation.
- `CHANGELOG.md` entry.

## Calls I made (check these)

1. Modbus slave address 1, 115200 8N1.
2. Link hold starts after 3 s without traffic (raised from 1 s on the
   Remote side's advice); hold window 60 s.
3. Five seconds of continuous safe readings before a clear is allowed;
   10 °C margin below both trip limits.
4. Clearing a fault requires naming the fault code you are clearing.
5. `air_target_cmd` writes are refused while a fault is latched; the target
   is 0 after the reset that a clear causes, and the Remote resends it.
6. Fault codes 0x03 (manual shutdown) and 0xFF (unknown) on the list as yes
   and no respectively.
7. TH2 kept as a reported, unused aux sensor rather than left floating.
8. `pwm_permille` instead of percent, so finer PWM steps later need no
   protocol change.

## Left to project 2 (Remote and Klipper)

- Already decided on the Remote side: polling runs in its own
  higher-priority task, started before Wi-Fi, so blocking network calls and
  the boot-time Wi-Fi wait never starve it.
- Whether the Remote clears faults on its own or only on a user action in
  its UI or from Klipper. The firmware's clear rule and the 3-per-power-cycle
  cap apply either way.
- How iHeater data reaches Klipper (for example `heater_virtual` taking
  temperature and power from the Remote instead of `chamber_hexa`).

## Left to the builder

- USART driver choice. LL or direct register access is recommended over
  HAL UART to save flash.
- Receive buffering (interrupt or DMA) and end-of-frame detection
  (`RTOR` or a timer).
- Exact LED timings.
- Moving the temperature-conversion code out of the main loop so the fault
  loop can share it.
- Linker-script reservation of the last flash page, or a build-time size
  check.
- A Modbus test plan: a PC with a USB-serial adapter and a standard Modbus
  tool covers every register and exception before the Remote work starts.

## Changes after approval

- USART1 uses the single-sample bit method (`CR3.ONEBIT`): HSI48 is untrimmed
  here, and ONEBIT raises the receiver's clock tolerance from about 3.3 % to
  about 3.9 %.
- The clear sequence verifies the erase. The order is decrement, clear the
  flash, read the fault page back, reply, reset. If the page still holds the
  fault, the reply is exception 0x04 for the same function code, the board does
  not reset, and the clear stays spent.
- `last_clear_result` is kept in `.noinit` with the clear counter and last
  fault, so it persists across the clear reset (power-on re-arms it to 0).
- `air_temp` (0x09) and `heater_temp` (0x0A) read `0x8000` when the reading is
  not finite or at or below the open-circuit limits (`MIN_AIR_TEMP`,
  `MIN_TEMP`), as `aux_temp` already did.
