#!/usr/bin/env python3
"""Bench probe for the iHeater UART build (Modbus RTU over USART1).

Needs pymodbus 3.x and pyserial:  pip install "pymodbus>=3,<4" pyserial

Usage:
  python tools/link_probe.py COM5 read        all registers, decoded
  python tools/link_probe.py COM5 target 60   set the air target in °C (0 = off)
  python tools/link_probe.py COM5 clear 7     clear active fault code 7
  python tools/link_probe.py COM5 raw         all registers undecoded, plus the
                                              frames on the wire

Exit code 0 on success, 1 on a timeout, a Modbus exception reply or any
other error, 2 on bad arguments.
"""

import argparse
import inspect
import sys

SLAVE = 1

INPUT_NAMES = [
    "proto_version", "fw_version", "fw_patch", "state", "active_fault",
    "fault_clearable_now", "clear_allow_mask", "remote_clears_left",
    "last_clear_result", "air_temp", "heater_temp", "aux_temp", "air_target",
    "heater_setpoint", "pwm_permille", "fan_on", "uptime_s_lo", "uptime_s_hi",
    "link_loss_count", "hold_remaining_s", "reset_cause", "last_fault",
]  # 0x00..0x15
CONTROL_NAMES = ["air_target_cmd", "clear_fault", "enter_bootloader"]  # 0x0100..
SETTING_NAMES = [
    "max_heater_temp", "max_air_temp", "min_target", "max_air_target",
    "link_hold_s", "delta", "fan_on_temp", "fan_off_temp",
    "fan_on_active_temp", "clear_margin",
]  # 0x0200..
INPUT_BASE, CONTROL_BASE, SETTING_BASE = 0x0000, 0x0100, 0x0200

TEMP_REGS = {
    "air_temp", "heater_temp", "aux_temp", "air_target", "heater_setpoint",
    "air_target_cmd", "max_heater_temp", "max_air_temp", "min_target",
    "max_air_target", "delta", "fan_on_temp", "fan_off_temp",
    "fan_on_active_temp", "clear_margin",
}

STATES = {0: "booting", 1: "idle", 2: "heating", 3: "link hold", 4: "fault latched"}
RESET_CAUSES = {0: "unknown", 1: "power-on", 2: "pin", 3: "software", 4: "watchdog"}
CLEAR_RESULTS = {
    0: "none yet", 1: "cleared", 2: "not on allowlist", 3: "readings not safe",
    4: "no clears left", 5: "code mismatch", 6: "no active fault",
}
FAULTS = {
    0x00: "none", 0x01: "air did not reach target in time",
    0x02: "heater not responding", 0x03: "manual shutdown",
    0x04: "air thermistor open", 0x05: "heater thermistor open",
    0x06: "heater over-temperature", 0x07: "air over-temperature",
    0x08: "link lost", 0xFF: "unknown",
}
MODBUS_EXCEPTIONS = {1: "illegal function", 2: "illegal address", 3: "illegal value"}


class ProbeError(Exception):
    pass


def temp_text(word):
    if word == 0x8000:
        return "n/a"
    signed = word - 0x10000 if word & 0x8000 else word
    return f"{signed / 10:.1f} °C"


def decode(name, word, regs):
    if name in TEMP_REGS:
        return temp_text(word)
    if name == "fw_version":
        return f"{word >> 8}.{word & 0xFF}"
    if name == "state":
        return f"{word} ({STATES.get(word, '?')})"
    if name in ("active_fault", "last_fault"):
        return f"0x{word:02X} ({FAULTS.get(word, '?')})"
    if name == "clear_allow_mask":
        codes = [f"0x{b:02X}" for b in range(16) if word >> b & 1]
        return f"0x{word:04X} ({', '.join(codes) or 'none'})"
    if name == "last_clear_result":
        return f"{word} ({CLEAR_RESULTS.get(word, '?')})"
    if name == "reset_cause":
        return f"{word} ({RESET_CAUSES.get(word, '?')})"
    if name == "pwm_permille":
        return f"{word} ({word / 10:.1f} %)"
    if name == "uptime_s_lo":
        return f"{word} (uptime {regs['uptime_s_hi'] << 16 | word} s)"
    if name in ("link_hold_s", "hold_remaining_s"):
        return f"{word} s"
    return str(word)


class Link:
    def __init__(self, port, baud, timeout, trace):
        try:
            from pymodbus.client import ModbusSerialClient
        except ImportError as exc:
            raise ProbeError(
                f"pymodbus 3.x is not installed ({exc}); "
                'run: pip install "pymodbus>=3,<4" pyserial'
            ) from exc
        if trace:
            try:
                from pymodbus import pymodbus_apply_logging_config

                pymodbus_apply_logging_config("DEBUG")  # logs SEND/RECV frames
            except ImportError:
                print("(this pymodbus cannot log frames)", file=sys.stderr)
        # RTU framing is the default for ModbusSerialClient in pymodbus 3.x.
        self.client = ModbusSerialClient(
            port=port, baudrate=baud, bytesize=8, parity="N", stopbits=1, timeout=timeout
        )
        # pymodbus renamed the unit keyword from slave= to device_id= during 3.x.
        params = inspect.signature(self.client.read_input_registers).parameters
        self.unit = {"device_id" if "device_id" in params else "slave": SLAVE}
        if not self.client.connect():
            raise ProbeError(f"cannot open {port}")

    def close(self):
        self.client.close()

    @staticmethod
    def _check(rr, what):
        if rr is None:
            raise ProbeError(f"{what}: no response")
        if rr.isError():
            code = getattr(rr, "exception_code", None)
            if code is not None:
                raise ProbeError(
                    f"{what}: Modbus exception 0x{code:02X} ({MODBUS_EXCEPTIONS.get(code, '?')})"
                )
            raise ProbeError(f"{what}: {rr} (timeout or bad reply)")
        return rr

    def read(self, holding, address, count):
        fn = self.client.read_holding_registers if holding else self.client.read_input_registers
        kind = "holding" if holding else "input"
        rr = self._check(fn(address, count=count, **self.unit), f"read {kind} 0x{address:04X}")
        if len(rr.registers) != count:
            raise ProbeError(f"read {kind} 0x{address:04X}: got {len(rr.registers)} of {count} registers")
        return rr.registers

    def write(self, address, value):
        self._check(self.client.write_register(address, value, **self.unit), f"write 0x{address:04X}")


def read_all(link):
    blocks = [
        ("Input registers", False, INPUT_BASE, INPUT_NAMES),
        ("Control (holding)", True, CONTROL_BASE, CONTROL_NAMES),
        ("Settings (holding)", True, SETTING_BASE, SETTING_NAMES),
    ]
    return [(title, base, names, link.read(holding, base, len(names)))
            for title, holding, base, names in blocks]


def cmd_read(link, decoded):
    for title, base, names, words in read_all(link):
        print(title)
        regs = dict(zip(names, words))
        for i, (name, word) in enumerate(zip(names, words)):
            text = decode(name, word, regs) if decoded else f"0x{word:04X} ({word})"
            print(f"  0x{base + i:04X} {name:<20} {text}")


def cmd_target(link, celsius):
    value = round(celsius * 10)
    if not 0 <= value <= 0xFFFF:
        raise ProbeError("target must be 0 (off) or a positive temperature")
    link.write(CONTROL_BASE, value)
    state = link.read(False, 0x03, 1)[0]
    target = link.read(False, 0x0C, 1)[0]
    print(f"air_target {temp_text(target)}, state {decode('state', state, {})}")


def cmd_clear(link, code):
    try:
        link.write(CONTROL_BASE + 1, code)
    except ProbeError:
        result = link.read(False, 0x08, 1)[0]
        print(f"clear refused: last_clear_result {decode('last_clear_result', result, {})}")
        raise
    print("clear accepted; the heater resets into a normal boot")


def fault_code(text):
    value = int(text, 16) if text.lower().startswith("0x") else int(text, 10)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError("fault code must be 0..0xFFFF")
    return value


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Probe an iHeater UART build over Modbus RTU (slave 1, 115200 8N1)."
    )
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=1.0, help="reply timeout in seconds")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("read", help="print all registers, decoded")
    p = sub.add_parser("target", help="write air_target_cmd (°C, 0 = off)")
    p.add_argument("celsius", type=float)
    p = sub.add_parser("clear", help="write clear_fault with the active fault code")
    p.add_argument("code", type=fault_code, help="fault code, e.g. 7 or 0x07")
    sub.add_parser("raw", help="print all registers undecoded and log the frames")
    args = ap.parse_args(argv)

    link = None
    try:
        link = Link(args.port, args.baud, args.timeout, trace=args.cmd == "raw")
        if args.cmd == "read":
            cmd_read(link, decoded=True)
        elif args.cmd == "raw":
            cmd_read(link, decoded=False)
        elif args.cmd == "target":
            cmd_target(link, args.celsius)
        elif args.cmd == "clear":
            cmd_clear(link, args.code)
    except ProbeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:  # timeouts and serial errors surface in several types across pymodbus 3.x
        print(f"error: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    finally:
        if link is not None:
            link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
