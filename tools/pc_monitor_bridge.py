"""Stream local Windows CPU/GPU telemetry to an explicitly selected DeskPet.

The board must identify itself as a supported PC-monitor firmware before *any*
telemetry is sent.  ``--dry-run`` reads local sensors without opening serial.
No network access, flash operation, system service, or auto-start is involved.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

import psutil
import serial
from serial.tools import list_ports


EXPECTED_VID = 0x303A
EXPECTED_PID = 0x1001
USB_SERIAL_ENV = "DESKPET_USB_SERIAL"
EXPECTED_VERSION = "deskpet-v01-20260929-r16-pc-monitor"
R17_VERSION = "deskpet-v01-20260929-r17-web-note"
R18_VERSION = "deskpet-v01-20260929-r18-web-keepalive"
R19_VERSION = "deskpet-v01-20260929-r19-onboard-wlan-pair"
R20_VERSION = "deskpet-v01-20260929-r20-wlan-manager"
R21_VERSION = "deskpet-v01-20260929-r21-card-transfer"
R22_VERSION = "deskpet-v01-20260929-r22-touch-finite"
SUPPORTED_VERSIONS = frozenset((EXPECTED_VERSION, R17_VERSION, R18_VERSION, R19_VERSION, R20_VERSION, R21_VERSION, R22_VERSION))
INTERVAL_S = 1.0
RECONNECT_S = 2.0
FIRMWARE_RETRY_S = 15.0
MAX_WIRE_BYTES = 159  # R16 board parser reserves 160 bytes including NUL.
NVIDIA_QUERY = (
    "name,utilization.gpu,temperature.gpu,memory.used,memory.total,"
    "power.draw,clocks.current.graphics"
)
CPU_TEMP_PS = (
    "$s = Get-CimInstance -Namespace 'root\\LibreHardwareMonitor' "
    "-ClassName Sensor -ErrorAction SilentlyContinue | "
    "Where-Object { $_.SensorType -eq 'Temperature' -and "
    r"($_.Name -match 'CPU Package|CPU \(Tctl/Tdie\)') } | "
    "Select-Object -First 1 -ExpandProperty Value; "
    "if ($null -ne $s) { [string]$s }"
)
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class PortMissing(Exception):
    pass


class WrongDevice(Exception):
    pass


class WrongFirmware(Exception):
    pass


class StatusTimeout(Exception):
    pass


class EventLogger:
    """Log connection state changes, not a lifetime of one-Hz measurements."""

    def __init__(self, path: Path | None = None, quiet: bool = False) -> None:
        self.path = path
        self.quiet = quiet
        self.last_key: str | None = None

    def event(self, key: str, message: str) -> None:
        if key == self.last_key:
            return
        self.last_key = key
        line = f"{datetime.now().astimezone().isoformat(timespec='seconds')} {key}: {message}"
        if not self.quiet:
            print(line, file=sys.stderr, flush=True)
        if self.path is not None:
            # Parent must already exist; no implicit filesystem tree creation.
            with self.path.open("a", encoding="utf-8", newline="\n") as log:
                log.write(line + "\n")


def bounded_int(value: str | float | int, low: int, high: int) -> int:
    """N/A, malformed, nonfinite, or impossible sensor values become -1."""
    try:
        number = float(value)
    except (TypeError, ValueError):
        return -1
    if not math.isfinite(number) or not low <= number <= high:
        return -1
    return int(round(number))


def ascii_name(name: str) -> str:
    """Keep a one-line, at most 40-byte name for the firmware's small buffer."""
    cleaned = re.sub(r"[^A-Za-z0-9 ._()+\-/]", " ", name)
    return " ".join(cleaned.split())[:40].strip() or "Unknown"


def cpu_name() -> str:
    try:
        import winreg

        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
        ) as key:
            return ascii_name(str(winreg.QueryValueEx(key, "ProcessorNameString")[0]))
    except (ImportError, OSError):
        return "Unknown CPU"


@dataclass(frozen=True)
class GpuSample:
    name: str = "GPU unavailable"
    use_pct: int = -1
    temp_c: int = -1
    vram_used_mib: int = -1
    vram_total_mib: int = -1
    power_w: int = -1
    core_mhz: int = -1


def parse_nvidia_row(output: str) -> GpuSample:
    rows = list(csv.reader(output.splitlines()))
    if not rows or len(rows[0]) != 7:
        raise ValueError("nvidia-smi did not return seven GPU fields")
    row = [part.strip() for part in rows[0]]
    return GpuSample(
        name=ascii_name(row[0]),
        use_pct=bounded_int(row[1], 0, 100),
        temp_c=bounded_int(row[2], 0, 125),
        vram_used_mib=bounded_int(row[3], 0, 2_000_000),
        vram_total_mib=bounded_int(row[4], 1, 2_000_000),
        power_w=bounded_int(row[5], 0, 2000),
        core_mhz=bounded_int(row[6], 0, 10_000),
    )


def read_nvidia() -> GpuSample:
    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                "--id=0",
                "--query-gpu=" + NVIDIA_QUERY,
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=1.5,
            creationflags=CREATE_NO_WINDOW,
            check=False,
        )
        if result.returncode == 0:
            return parse_nvidia_row(result.stdout)
    except (OSError, subprocess.TimeoutExpired, ValueError):
        pass
    return GpuSample()


class CpuTempReader:
    """Read only a named CPU package sensor exposed by LibreHardwareMonitor.

    Windows ACPI thermal zones and motherboard sensors are deliberately not
    treated as CPU temperature.  The optional WMI probe is infrequent and
    timeout-bounded; absence is represented by -1.
    """

    def __init__(self) -> None:
        self.value = -1
        self.next_probe = 0.0

    def read(self, now: float) -> int:
        if now < self.next_probe:
            return self.value
        try:
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", CPU_TEMP_PS],
                capture_output=True,
                text=True,
                timeout=1.0,
                creationflags=CREATE_NO_WINDOW,
                check=False,
            )
            self.value = bounded_int(result.stdout.strip().replace(",", "."), 0, 125) if result.returncode == 0 else -1
        except (OSError, subprocess.TimeoutExpired):
            self.value = -1
        self.next_probe = now + (5.0 if self.value >= 0 else 30.0)
        return self.value


@dataclass(frozen=True)
class Snapshot:
    cpu_pct: int
    cpu_mhz: int
    cpu_temp_c: int
    gpu_pct: int
    gpu_temp_c: int
    vram_used_mib: int
    vram_total_mib: int
    gpu_power_w: int
    gpu_core_mhz: int

    def wire(self) -> str:
        return "pcmon " + " ".join(str(number) for number in (
            self.cpu_pct, self.cpu_mhz, self.cpu_temp_c, self.gpu_pct,
            self.gpu_temp_c, self.vram_used_mib, self.vram_total_mib,
            self.gpu_power_w, self.gpu_core_mhz,
        ))


class Collector:
    def __init__(self) -> None:
        self.cpu_model = cpu_name()
        self.physical_cores = psutil.cpu_count(logical=False) or -1
        self.logical_cores = psutil.cpu_count(logical=True) or -1
        self.gpu = read_nvidia()
        self.cpu_temp = CpuTempReader()
        psutil.cpu_percent(interval=None)  # Prime the per-interval Windows CPU counter.

    def refresh_gpu(self) -> None:
        self.gpu = read_nvidia()

    def info_lines(self) -> tuple[str, str, str]:
        return (
            "pcinfo cpu " + self.cpu_model,
            "pcinfo gpu " + self.gpu.name,
            f"pcinfo cores {self.physical_cores} {self.logical_cores}",
        )

    def sample(self, now: float) -> Snapshot:
        self.gpu = read_nvidia()
        # On this Windows host psutil.cpu_freq() and Processor Frequency both
        # stay at 3400 MHz while % Processor Performance changes.  That value
        # is nominal, not a reliable live clock, so the wire value is -1.
        return Snapshot(
            cpu_pct=bounded_int(psutil.cpu_percent(interval=None), 0, 100),
            cpu_mhz=-1,
            cpu_temp_c=self.cpu_temp.read(now),
            gpu_pct=self.gpu.use_pct,
            gpu_temp_c=self.gpu.temp_c,
            vram_used_mib=self.gpu.vram_used_mib,
            vram_total_mib=self.gpu.vram_total_mib,
            gpu_power_w=self.gpu.power_w,
            gpu_core_mhz=self.gpu.core_mhz,
        )


def resolve_usb_serial(value: str | None) -> str:
    """CLI input takes precedence over the environment; no device is assumed."""
    selected = os.environ.get(USB_SERIAL_ENV) if value is None else value
    if selected is None or not selected.strip():
        raise ValueError(
            "Serial mode requires --usb-serial or DESKPET_USB_SERIAL; "
            "use --dry-run for local sensors without a board."
        )
    return selected.strip()


def select_port(forced_port: str | None, expected_serial: str) -> str:
    """Match VID/PID and the user's serial; an explicit port is still gated."""
    if not expected_serial or not expected_serial.strip():
        raise ValueError("A nonempty USB serial is required before port discovery.")
    ports = list(list_ports.comports())
    if forced_port is not None:
        selected = [item for item in ports if item.device.upper() == forced_port.upper()]
        if not selected:
            raise PortMissing(f"forced port {forced_port} is absent")
        item = selected[0]
        if (item.vid, item.pid, item.serial_number) != (
            EXPECTED_VID, EXPECTED_PID, expected_serial
        ):
            raise WrongDevice(
                f"{forced_port} USB identity mismatch; the port must match the "
                "expected VID/PID and your configured USB serial."

            )
        return item.device
    matching = [
        item for item in ports
        if (item.vid, item.pid, item.serial_number)
        == (EXPECTED_VID, EXPECTED_PID, expected_serial)
    ]
    if not matching:
        raise PortMissing("configured DeskPet USB identity is absent")
    if len(matching) != 1:
        raise WrongDevice("multiple ports claim the exact DeskPet USB identity; refusing to guess")
    return matching[0].device


def open_serial(port: str) -> serial.Serial:
    # Set idle line states before open.  Never toggle them or send reset pulses.
    connection = serial.Serial(
        port=None, baudrate=115200, timeout=0.2, write_timeout=1.0
    )
    connection.dtr = False
    connection.rts = False
    connection.port = port
    try:
        connection.open()
    except BaseException:
        connection.close()
        raise
    return connection


def send_line(connection: serial.Serial, line: str) -> None:
    packet = (line + "\n").encode("ascii", errors="strict")
    if len(packet) > MAX_WIRE_BYTES:
        raise ValueError(f"wire line exceeds {MAX_WIRE_BYTES} bytes")
    if connection.write(packet) != len(packet):
        raise serial.SerialTimeoutException("Incomplete USB write")


def check_firmware(
    connection: serial.Serial, expected_versions: str | frozenset[str]
) -> dict:
    """Only `status` may be sent before an explicit allowlist version match."""
    allowed = (
        frozenset((expected_versions,))
        if isinstance(expected_versions, str)
        else expected_versions
    )
    send_line(connection, "status")
    deadline = time.monotonic() + 4.0
    pending = bytearray()
    while time.monotonic() < deadline:
        chunk = connection.read(256)
        if not chunk:
            continue
        pending.extend(chunk)
        if len(pending) > 8192:
            raise StatusTimeout("status response exceeded 8192 bytes")
        while b"\n" in pending:
            raw, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            try:
                value = json.loads(raw.decode("utf-8", errors="replace"))
            except (ValueError, UnicodeError):
                continue
            if not isinstance(value, dict) or not isinstance(value.get("version"), str):
                continue
            observed = value["version"]
            if observed not in allowed:
                raise WrongFirmware(
                    f"board has {observed!r}, requires one of {sorted(allowed)!r}; "
                    "nothing but read-only status was sent"
                )
            return value
    raise StatusTimeout("no versioned JSON status within 4 seconds; no telemetry sent")


def positive_samples(value: str) -> int:
    result = int(value)
    if not 1 <= result <= 3600:
        raise argparse.ArgumentTypeError("--samples must be from 1 to 3600")
    return result


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--usb-serial",
        help="Required for serial mode, or set DESKPET_USB_SERIAL; no device serial is built in",
    )
    result.add_argument(
        "--port", help="Optional forced COM port; still verifies VID/PID and the configured USB serial"
    )
    result.add_argument("--samples", type=positive_samples, help="Stop after N telemetry frames; default runs until Ctrl+C")
    result.add_argument("--dry-run", action="store_true", help="Read local sensors only, without opening any serial port")
    result.add_argument("--quiet", action="store_true", help="Suppress per-second console lines for hidden background use")
    result.add_argument("--event-log", type=Path, help="Append connection state changes to an existing directory")
    return result


def wait_until(target: float) -> None:
    delay = target - time.monotonic()
    if delay > 0:
        time.sleep(delay)


def stream_samples(collector: Collector, connection: serial.Serial | None, count: int | None,
                   quiet: bool = False) -> int:
    sent = 0
    next_due = time.monotonic() + INTERVAL_S
    while count is None or sent < count:
        wait_until(next_due)
        sample = collector.sample(time.monotonic())
        line = sample.wire()
        if connection is not None:
            send_line(connection, line)
            # The board may emit unsolicited diagnostics.  Drain them so its
            # USB TX buffer cannot fill indefinitely while we stream.
            waiting = min(connection.in_waiting, 4096)
            if waiting:
                connection.read(waiting)
        if not quiet:
            print(line, flush=True)
        sent += 1
        next_due += INTERVAL_S
        if next_due < time.monotonic():
            next_due = time.monotonic() + INTERVAL_S
    return sent


def run(args: argparse.Namespace, logger: EventLogger | None = None) -> int:
    expected_serial = None if args.dry_run else resolve_usb_serial(args.usb_serial)
    logger = logger or EventLogger()
    collector = Collector()
    if args.dry_run:
        for line in collector.info_lines():
            print(line, flush=True)
        stream_samples(collector, None, args.samples or 3)
        return 0

    logger.event("started", f"Waiting for exact board ({args.port or 'automatic COM discovery'})")
    sent_total = 0
    while args.samples is None or sent_total < args.samples:
        try:
            port = select_port(args.port, expected_serial)
        except PortMissing as error:
            logger.event("unplugged", str(error))
            time.sleep(RECONNECT_S)
            continue
        except WrongDevice as error:
            logger.event("wrong_device", str(error))
            time.sleep(FIRMWARE_RETRY_S)
            continue

        connection: serial.Serial | None = None
        retry_after = RECONNECT_S
        try:
            connection = open_serial(port)
            status = check_firmware(connection, SUPPORTED_VERSIONS)
            logger.event(
                "connected",
                f"Verified {port}: {status['version']}; sending telemetry once per second",
            )
            collector.refresh_gpu()  # Refresh the model after a long unplugged wait.
            for line in collector.info_lines():
                send_line(connection, line)
            remaining = None if args.samples is None else args.samples - sent_total
            sent_total += stream_samples(collector, connection, remaining, quiet=args.quiet)
        except WrongFirmware as error:
            logger.event("wrong_firmware", str(error))
            retry_after = FIRMWARE_RETRY_S
        except (StatusTimeout, OSError, serial.SerialException, ValueError) as error:
            logger.event("link_error", f"{error}; retrying USB discovery")
        finally:
            if connection is not None and connection.is_open:
                connection.close()
        if args.samples is None or sent_total < args.samples:
            time.sleep(retry_after)
    logger.event("completed", f"Sent {sent_total} samples and closed the USB port")
    return 0


def main() -> int:
    args = parser().parse_args()
    logger = EventLogger(args.event_log, args.quiet)
    if os.name != "nt":
        print("This bridge requires Windows.", file=sys.stderr)
        return 2
    try:
        return run(args, logger)
    except ValueError as error:
        print(f"Configuration error: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        logger.event("stopped", "Stopped by user; serial port closed")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
