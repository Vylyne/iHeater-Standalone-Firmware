# AGENTS.md

Ground rules for anyone (human or agent) changing this repo. Read before editing.

## What this is

Standalone firmware for the iHeater chamber heater: STM32F042F6 (32 KB flash, 6 KB RAM, HSI48), STM32Cube HAL, PlatformIO. It is a fork of `pavluchenkor/iHeater-Standalone-Firmware`, so keep changes easy to send upstream.

## Builds

`platformio.ini` lists five environments; all of them must build:

| Env | What it is |
|---|---|
| `iheater_rev1_0`, `iheater_rev1_1` | Analog trigger builds |
| `idryer_rev1_1` | iDryer profile (currently the same binary as iheater, because `config.h` overrides `DEVICE_PROFILE`) |
| `iheater_rev1_1_pulse` | Setpoint by pulse train on TH2 (iHeater-link) |
| `iheater_rev1_1_uart` | Modbus RTU link to iHeater Remote over USB-C D+/D− |

Builds really compile at `-Og`: PlatformIO's debug build type drops `-O0`.

## Gates (run before every commit)

```sh
pio run                 # all 5 envs must succeed
pio test -e native      # host unit tests (Unity); all must pass
```

`pio test -e native` needs a host gcc on PATH (on Windows, MinGW from scoop: `export PATH="$HOME/scoop/apps/mingw/current/bin:$PATH"`).

## Hard constraints

- **Flash:** the UART image must stay at or under 30720 B (`board_upload.maximum_size`). The last two pages are reserved: NVS (trigger calibration) at 0x08007800 and the stored fault code at 0x08007C00. The linker script is `ld/STM32F042F6_iheater_link.ld`.
- **Watchdog:** IWDG is about 3.2 s. Every loop, including fault loops and anything that waits, must refresh it.
- **Heater safety:**
  - No new code path may turn the heater on while a fault is latched.
  - Faults fail closed.
  - Which faults the link may clear is set only by `FAULT_TABLE` in `Core/Inc/config.h`; codes above 0x0F are never remotely clearable.
- **Existing builds:** changes for one input mode go under `#if INPUT_MODE == ...`. The other builds must behave as before.
- **Logic:** protocol and policy logic lives in HAL-free code under `Lib/` and gets native tests. HAL glue stays thin (`Core/Src/link_uart.c`, `Core/Src/link_app.c`).

## Hardware checks

The native tests cannot cover the chip. Bench-check changes to:

- link timing
- flash
- reset
- LEDs

Use these:

- `tools/link_probe.py` (pymodbus 3.x + pyserial)
- `Testing protocol.md` (fault scenarios, in Russian)

## Docs

- User docs live in `docs/<lang>/README.md`.
  - `en` and `ru` are full and must stay in step.
  - The other languages are `i18n-placeholder` stubs. Leave them alone unless you are translating the whole page.
- Design specs and plans go in `docs/design/YYYY-MM-DD-<topic>-{design,plan}.md`.
- Add user-visible changes to `CHANGELOG.md` under **Unreleased**, in its existing style.
- Write new English text in Canadian spelling.

## Git

- Conventional-style subjects: `feat(link): …`, `fix: …`, `docs: …`, `refactor: …`, `build: …`.
- Work on a feature branch, never directly on `main`. Do not push without the owner's say-so.
- **Line endings:**
  - `CHANGELOG.md` is stored with CRLF.
  - With `core.autocrlf=true`, a rewrite can silently turn the whole file into LF.
  - After committing, check `git diff --stat` and confirm only the lines you meant to change appear.
- Never commit build outputs (`Firmware/`, `.pio/`, `*.bin`, `*.elf`, `*.hex`).
