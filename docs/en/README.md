<!-- [![Release](https://img.shields.io/github/v/release/pavluchenkor/iHeater-Standalone-Firmware?include_prereleases&label=latest)](https://github.com/pavluchenkor/iHeater-Standalone-Firmware/releases) -->
<!-- [![Docs](https://img.shields.io/badge/docs-view--online-green?logo=readthedocs)](https://docs.idryer.org/iHeater/) -->
<!-- [![Telegram](https://img.shields.io/badge/Telegram-Join%20Chat-blue?logo=telegram)](https://t.me/iDryer) -->
<!-- [![Discord](https://img.shields.io/badge/discord-Join%20Chat-5865F2?logo=discord)](https://discord.gg/jGce5eeHHz) -->


# Standalone iHeater Firmware

iHeater is a compact and affordable solution for 3D printers without an active heated chamber or with limited connection options to the mainboard. It's especially useful for printers with proprietary boards lacking free ports for fans, heaters, or thermistors. iHeater solves this with a simple and universal approach.

It can operate standalone with its own firmware or be integrated with Klipper over USB.

![iHeater](../img/iHeater_200.png)

---

## About the Firmware

**iHeater** is firmware for a standalone device that manages the temperature of a 3D printer's heated chamber.

---

## Purpose

The device ensures heating and stable temperature maintenance inside the printer's chamber, which is critical for printing ABS, ASA, and other shrinkage-sensitive and adhesion-sensitive filaments.

---

## How to Configure the Firmware

### File `config.h`

This file contains all configurable parameters:

| Parameter                                 | Description                                                                                   |
| ----------------------------------------- | --------------------------------------------------------------------------------------------- |
| `SELECTED_THERMISTOR_TYPE`                | Select thermistor type (0-5, see below)                                                       |
| `MODE_TEMP_0`-`MODE_TEMP_7`                           | Setpoint temperatures for each mode. Can be any float value, e.g. `MODE_TEMP_4 67.5`                |
| `MAX_TEMP`, `MIN_TEMP`                    | Allowed temperature range for the heater                                                      |
| `MAX_AIR_TEMP`, `MIN_AIR_TEMP`            | Allowed temperature range for air                                                             |
| `HEATER_MIN_PWM`                          | Minimum PWM required to start heater response monitoring                                      |
| `HEATER_RESPONSE_TIMEOUT_MS`              | Timeout for detecting heater response (default 5 seconds)                                     |
| `AIR_TARGET_TIMEOUT_MS`                   | Maximum time allowed to reach air target temperature (default 10 minutes)                    |
| `TIMER_CLOCK`, `PWM_FREQUENCY`, `PWM_STEPS` | PWM configuration (frequency and resolution)                                                |

### Thermistors

Selected using `SELECTED_THERMISTOR_TYPE`:

| ID | Type                          | Notes                              |
|----|-------------------------------|-------------------------------------|
| 0  | ATC Semitec 104GT-2           | Standard used by E3D                |
| 1  | ATC Semitec 104NT-4-R025H42G  | 104GT alternative                   |
| 2  | EPCOS 100K B57560G104F        | Widely used                         |
| 3  | Generic 3950                  | **Default**, common Chinese type    |
| 4  | SliceEngineering 450          | High-temp support                   |
| 5  | TDK NTCG104LH104JT1           | Medical-grade accuracy              |

### Temperature Modes and LEDs

Each mode sets a target temperature and is displayed via binary LEDs:

When entering the mode, the LEDs blink, once the mode is activated, they remain constantly lit.

| Mode   | Temperature | LED3 | LED2 | LED1 |
|--------|-------------|------|------|------|
| MODE_TEMP_0  | 0.0°C       |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| MODE_TEMP_1  | 55.0°C      |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |
| MODE_TEMP_2  | 60.0°C      |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| MODE_TEMP_3  | 65.0°C      |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |
| MODE_TEMP_4  | 70.0°C      |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| MODE_TEMP_5  | 75.0°C      |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |
| MODE_TEMP_6  | 80.0°C      |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| MODE_TEMP_7  | 85.0°C      |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |![LED 1Hz](../img/ball_gifs/blinking_ball.gif)   |


---
### Automatic enabling and disabling r1.1

Macros

```
#define TRIGGER_ON_TEMP  45.0f
#define TRIGGER_OFF_TEMP 80.0f
#define TRIGGER_MODE MODE_2
```

These parameters define the activation and deactivation temperatures for the heater, as well as the mode to be enabled upon activation.

Assuming the trigger thermistor is located near the heated bed.
With the current settings, the chamber heater will turn on when the bed temperature reaches 45°C and will turn off when the bed temperature drops below 80°C. These parameters are suitable for most filaments that require an actively heated chamber and do not cause logical conflicts during operation.

It is also possible to select the mode that will be activated when the trigger is engaged. For example, TRIGGER_MODE MODE_2 is currently selected, which means that MODE_TEMP_2 will be activated, setting the chamber temperature to 60.0°C.


---
### Working with iHeater-link (r1.1 pulse build)

The `iheater_rev1_1_pulse` build is designed to pair with the ESP-based iHeater-link module. The module receives commands from the printer, a portal, or a mobile application and forwards the chamber setpoint to iHeater over a single GPIO line. The iHeater firmware is transport-agnostic — it only reads a number: the desired chamber air temperature in °C.

#### Wiring

1. Flash iHeater with the `iheater_rev1_1_pulse` build (see "How to Flash the Firmware").
2. Connect the signal wire from iHeater-link to the **TH2** connector on the iHeater board (pin PB1).
3. A common GND between the ESP and iHeater is mandatory — use a dedicated ground wire.
4. Keep the heater thermistor on its usual **TH0** connector. Keep the air thermistor on **TH1**. Both work exactly as in any other build.
5. The trigger connector is not used in this build (its line is repurposed as the digital input).

!!! warning "Never connect or disconnect the signal wire with power applied."

#### Accepted commands

| Code from iHeater-link | Meaning |
|------------------------|---------|
| `0`                    | Heater off |
| `10`                   | Heater off (agreed "off-code") |
| `45..90`               | Heater on, air setpoint equals the code in °C |
| Any other value        | Ignored; previous state is kept |

The minimum `45 °C` is defined in `config.h` as `PULSE_MIN_TARGET`. The maximum is bounded by `MAX_AIR_TEMP` (90 °C by default).

#### LED indication

| State | LED1 | LED2 | LED3 |
|-------|------|------|------|
| Waiting for the first frame after boot (up to 20 s) | chasing light LED1 → LED2 → LED3 with a 1000 ms step | | |
| Link OK, heating off | solid on (link-alive heartbeat) | off | short ~80 ms flash on every accepted frame |
| Link OK, heating on  | chasing light LED1 → LED2 → LED3 with a 500 ms step | | |
| Link lost (>1.5 s with no frame after the first one) | All three blink together; the device resets with error code `0x08` (`ERROR_LINK_LOST`). The heater stays off. | | |

Standard faults (overheat, open thermistor, heater-no-response) work exactly as in analog builds — see "Error Handling".

#### Link-lost behaviour

iHeater and iHeater-link can be powered up simultaneously. iHeater-link needs time to bring up Wi-Fi and connect to its command source, so for the first **20 seconds** after boot the firmware does **not** treat the absence of frames as a fault and shows a slow chasing light (1000 ms step) — "waiting for the first frame".

If no valid frame is received during this 20-second window, the firmware enters `ERROR_LINK_LOST` exactly as it would after a runtime drop.

Once the first frame has been received, the normal rule applies: a gap of more than 1.5 seconds with no frames triggers the fault.

When the fault fires:

1. The firmware persists error code `0x08` (`ERROR_LINK_LOST`) in flash.
2. The MCU is reset.
3. After the reset the heater is off; the firmware again opens a 20-second waiting window with the slow chasing light, then falls back to the slow synchronous blink of all three LEDs.
4. To clear the error, hold the MODE button while powering the device on, then release it. The device returns to normal operation.

#### MODE button

In the pulse build, the MODE button **does not cycle modes and does not trigger calibration**. Its only purpose is clearing a persisted error at startup. All heating commands come from iHeater-link.

---

### Working with iHeater Remote (UART build)

The `iheater_rev1_1_uart` build lets a Remote (an ESP module, a single-board computer, a USB serial adapter) drive iHeater over a Modbus RTU link on USART1. The Remote can set the chamber air temperature, read the state and both temperatures, and clear some faults. The image is limited to 30 KB (the last two flash pages hold the trigger calibration and the fault code); it is currently about 28.2 KB.

#### Wiring

The link uses the USB-C connector pins, not USB data. 3.3 V logic.

| USB-C pin | Signal | MCU pin |
|-----------|--------|---------|
| VBUS | 5 V | power in |
| GND | GND | GND |
| D− | iHeater TX | PA9 (pin 17) |
| D+ | iHeater RX | PA10 (pin 18) |

Cross the data lines: the Remote's TX goes to iHeater RX (D+), and the Remote's RX goes to iHeater TX (D−).

!!! warning "The board has no 5 V regulator. Power it from the Remote's 5 V (VBUS) only. Never feed 5 V from USB and from the JST 5 V connector at the same time."

!!! warning "Never connect or disconnect the link cable with power applied."

Keep the heater thermistor on **TH0** and the air thermistor on **TH1**. The trigger feature is disabled in this build.

#### Serial settings

Modbus RTU, slave address 1, 115200 baud, 8N1. Function codes 03, 04, 06 and 10 are supported; errors are reported as exceptions 0x01 (function), 0x02 (address or state) and 0x03 (value). Frames for other addresses, frames with a bad CRC and function codes with bit 0x80 set get no reply. Temperatures on the wire are signed 16-bit tenths of °C; an unreadable sensor reads `0x8000`. The full register map is in [docs/design/2026-11-19-uart-link-design.md](../design/2026-11-19-uart-link-design.md).

#### Hold behaviour

At boot the target is 0 until the Remote writes one. After **3 s** without a valid frame iHeater enters hold and keeps the last target; **60 s** later (63 s after the last frame) the target drops to 0. This is not a fault, and heating restarts only on a new target.

#### LED indication

| State | LEDs |
|-------|------|
| Waiting for link (no frame yet, or after the hold expired) | slow chasing light, 1000 ms step |
| Idle (target 0) | LED1 solid, short LED3 flash on every answered frame |
| Heating | chasing light, 500 ms step |
| Hold (link silent) | all three blink together at 4 Hz |
| Fault | the usual fault patterns, see "Error Handling" |

#### Faults and remote clearing

| Code | Fault | Remote clear |
|------|-------|--------------|
| `0x01` | Temperature timeout | yes |
| `0x02` | Heater not responding | no |
| `0x03` | Manual shutdown | yes |
| `0x04` | Air thermistor open | yes |
| `0x05` | Heater thermistor open | yes |
| `0x06` | Heater over-temperature | no |
| `0x07` | Air over-temperature | yes |
| `0x08` | Link lost | yes |
| `0xFF` | Unknown | no |

The table is the `FAULT_TABLE` in `config.h`. A fault stays latched until it is cleared. To clear it remotely, write the active fault code to holding register `0x0101`. The write succeeds only if all of these hold:

- the code equals the active fault and that fault is marked "yes" above;
- remote clears are left (3 per power cycle; a power cycle re-arms them);
- the readings have been safe for 5 s: both thermistors valid, air at least 10 °C below `MAX_AIR_TEMP`, heater element at least 10 °C below `MAX_TEMP`.

On success the board replies, clears the stored fault and restarts; the target is 0 after the restart. Holding MODE at power-on still clears a fault locally.

Notes for the Remote side:

- Clear with a single-register write (function 06). An FC 10 write that spans `0x0101` and later registers may clear the fault and restart the board even though the reply is an exception.
- A multi-register write that is refused part-way keeps the registers written before the refusal. Re-read after any exception.
- If the flash erase fails, the board has still replied with success; it restarts back into the fault with one clear used. Re-read the state after the restart.

#### Bench probe

`tools/link_probe.py` needs Python with `pymodbus` 3.x and `pyserial` (`pip install "pymodbus>=3,<4" pyserial`):

```
python tools/link_probe.py COM5 read        # all registers, decoded
python tools/link_probe.py COM5 target 60   # set the air target in °C (0 = off)
python tools/link_probe.py COM5 clear 7     # clear active fault code 7
python tools/link_probe.py COM5 raw         # undecoded registers plus the frames on the wire
```

The exit code is 0 on success, 1 on a timeout or error reply, 2 on bad arguments.

---

## How to Flash the Firmware

### Download

[Скачайте прошивку со страницы релизов](https://github.com/pavluchenkor/iHeater-Standalone-Firmware/releases)


### Using Precompiled Binary

[Download the firmware from the releases page](https://github.com/pavluchenkor/iHeater-Standalone-Firmware/releases)


This firmware is pre-configured with the temperature modes from MODE_TEMP_0 to MODE_TEMP_7 (as described above) and designed to work with a Generic 3950 thermistor (type 3 in `config.h`).

### Using DFU Mode (USB)

=== "r1"

    1. Set the BOOT0 jumper
    2. Connect USB, the device will be recognized as DFU 3.1 Upload the firmware using STM32CubeProgrammer:

    3.1.1 Specify the path to the `.bin` firmware file (e.g.: `Firmware/iHeater_v1.2.3.bin`) 3.1.2 Set the download address: `0x08000000`

    3.1.3 Click "Start Programming"

    or

    3.2 Upload the firmware via command line:

    ```bash
    dfu-util -a 0 -s 0x08000000 -D Firmware/iHeater_vX.Y.Z.bin
    ```

    4. Remove the jumper and power cycle the device

=== "r1.1"

    1. Press and hold the BOOT button
    2. Connect USB, the device will be recognized as DFU 
    3. Upload the firmware using STM32CubeProgrammer:
    
        3.1.1 Specify the path to the `.bin` firmware file (e.g.: `Firmware/iHeater_v1.2.3.bin`)

        3.1.2 Set the download address: `0x08000000`

        3.1.3 Click "Start Programming"

        or

        3.2 Upload the firmware via command line:

        ```bash
        dfu-util -a 0 -s 0x08000000 -D Firmware/iHeater_vX.Y.Z.bin
        ```

    4. Power cycle the device


---

## How to Use

=== "r1"

    * **Short press** of the MODE button - switches modes (0-7)
    * **Long press (2+ sec)** - resets the mode to 0 (OFF)
    * **LED indicators** display the current mode according to the table above
    * **Flashing LEDs** - mode is active but target temperature not yet reached
    * **Solid light** - target temperature reached

=== "r1.1"

    !!! note "Revision Differences"

        revision r1.1 features a trigger port for automatic chamber heating control. When a thermistor is connected to the trigger port, heating will turn on and off according to the temperatures specified in config.h, by default 45 and 80 for heating activation and deactivation.


    * **Short press** of the MODE button - switches modes (0-7)
    * **Long press (2+ sec)** - resets the mode to 0 (OFF)
    * **LED indicators** display the current mode according to the table above
    * **Flashing LEDs** - mode is active but target temperature not yet reached
    * **Solid light** - target temperature reached

---

## Error Handling

The device auto-resets and shows an error code using LED indicators. The fan will be switched on at the same time.

| Code  | Description                               | LED3 | LED2 | LED1  |
|-------|-------------------------------------------|------|------|-------|
| 0x01  | Air failed to reach target in time        |![LED OFF](../img/ball_gifs/black_ball.gif)  |![LED OFF](../img/ball_gifs/black_ball.gif) |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |
| 0x02  | Heater not responding                     |![LED OFF](../img/ball_gifs/black_ball.gif) |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED OFF](../img/ball_gifs/black_ball.gif)  |
| 0x03  | Manual shutdown (unused)                  |![LED OFF](../img/ball_gifs/black_ball.gif)  |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |
| 0x04  | Air thermistor disconnected                |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif) |![LED OFF](../img/ball_gifs/black_ball.gif) |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| 0x05  | Heater thermistor disconnected             |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED OFF](../img/ball_gifs/black_ball.gif) |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)   |
| 0x06  | Heater overtemperature                     |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif) |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED OFF](../img/ball_gifs/black_ball.gif)   |
| 0x07  | Air overtemperature                        |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)  |![LED 10Hz](../img/ball_gifs/blinking_ball_10hz.gif)   |
| 0x08  | iHeater-link lost (pulse build only)       | 0.25 Hz | 0.25 Hz | 0.25 Hz |
| 0xFF  | Unknown error                              |![LED ON](../img/ball_gifs/red_ball.gif)  | ![LED ON](../img/ball_gifs/red_ball.gif)|![LED ON](../img/ball_gifs/red_ball.gif)  |


### Resetting an Error

Hold the MODE button during startup until the LEDs change. The stored error will be cleared from flash memory, and the device will resume normal operation.

---

## Fine Tuning

If there is confirmation that the thermistor readings deviate from the actual temperature, the fine tuning function can be used. For this purpose, it is necessary to measure the resistance of the corresponding resistors and record these values instead of the default ones.


```C++ title="Actual resistance of the pull-up resistors"
#define PULLUP_TH0 4700.0f
#define PULLUP_TH1 4700.0f
#define PULLUP_TH2 4700.0f
```

---

This software is licensed under the GNU General Public License v3.0 for non-commercial use only.