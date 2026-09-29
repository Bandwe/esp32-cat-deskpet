"""Isolated host tests; all USB discovery and serial connections are mocked."""

from __future__ import annotations

import argparse
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pc_monitor_bridge as bridge  # noqa: E402


TEST_USB_SERIAL = "TEST-DESKPET-001"  # Synthetic fixture, never a real board ID.


class FakePort:
    def __init__(self, version: str) -> None:
        self.response = ('{"version":"' + version + '"}\n').encode("ascii")
        self.written: list[bytes] = []
        self.is_open = True
        self.in_waiting = 0

    def write(self, data: bytes) -> int:
        self.written.append(data)
        return len(data)

    def read(self, size: int) -> bytes:
        result = self.response[:size]
        self.response = self.response[size:]
        return result

    def close(self) -> None:
        self.is_open = False


class GpuParsingTests(unittest.TestCase):
    def test_realistic_nvidia_row(self) -> None:
        gpu = bridge.parse_nvidia_row(
            "NVIDIA GeForce RTX 5070 Ti, 10, 35, 3365, 16303, 16.95, 86\n"
        )
        self.assertEqual(gpu.name, "NVIDIA GeForce RTX 5070 Ti")
        self.assertEqual((gpu.use_pct, gpu.temp_c), (10, 35))
        self.assertEqual((gpu.vram_used_mib, gpu.vram_total_mib), (3365, 16303))
        self.assertEqual((gpu.power_w, gpu.core_mhz), (17, 86))

    def test_unavailable_fields_do_not_become_zero(self) -> None:
        gpu = bridge.parse_nvidia_row(
            "GPU, N/A, N/A, N/A, 16303, N/A, N/A\n"
        )
        self.assertEqual(gpu.use_pct, -1)
        self.assertEqual(gpu.temp_c, -1)
        self.assertEqual(gpu.vram_used_mib, -1)
        self.assertEqual(gpu.vram_total_mib, 16303)
        self.assertEqual(gpu.power_w, -1)

    def test_snapshot_has_nine_integer_fields(self) -> None:
        sample = bridge.Snapshot(5, -1, -1, 10, 35, 3365, 16303, 17, 86)
        self.assertEqual(sample.wire(), "pcmon 5 -1 -1 10 35 3365 16303 17 86")
        self.assertEqual(len(sample.wire().split()), 10)

    def test_model_name_is_one_line_ascii_40_bytes(self) -> None:
        result = bridge.ascii_name("Intel\n❤\t" + "x" * 100)
        self.assertLessEqual(len(result.encode("ascii")), 40)
        self.assertNotIn("\n", result)



class ConfigurationTests(unittest.TestCase):
    def test_no_device_serial_is_built_in(self) -> None:
        with mock.patch.dict(bridge.os.environ, {}, clear=True):
            args = bridge.parser().parse_args([])
            self.assertIsNone(args.usb_serial)
            with self.assertRaisesRegex(ValueError, "--usb-serial"):
                bridge.resolve_usb_serial(args.usb_serial)

    def test_cli_serial_overrides_environment(self) -> None:
        with mock.patch.dict(bridge.os.environ, {bridge.USB_SERIAL_ENV: "TEST-ENV-DEVICE"}):
            args = bridge.parser().parse_args(["--usb-serial", "TEST-CLI-DEVICE"])
            self.assertEqual(bridge.resolve_usb_serial(args.usb_serial), "TEST-CLI-DEVICE")

    def test_environment_serial_is_used_when_cli_omits_it(self) -> None:
        with mock.patch.dict(bridge.os.environ, {bridge.USB_SERIAL_ENV: " TEST-ENV-DEVICE "}):
            args = bridge.parser().parse_args([])
            self.assertEqual(bridge.resolve_usb_serial(args.usb_serial), "TEST-ENV-DEVICE")

    def test_explicit_empty_serial_does_not_fall_back_to_environment(self) -> None:
        with mock.patch.dict(bridge.os.environ, {bridge.USB_SERIAL_ENV: "TEST-ENV-DEVICE"}):
            with self.assertRaises(ValueError):
                bridge.resolve_usb_serial("   ")

    def test_missing_serial_fails_before_sensors_or_usb_discovery(self) -> None:
        args = bridge.parser().parse_args([])
        with (
            mock.patch.dict(bridge.os.environ, {}, clear=True),
            mock.patch.object(bridge, "Collector") as collector,
            mock.patch.object(bridge.list_ports, "comports") as discover,
            mock.patch.object(bridge, "open_serial") as open_serial,
        ):
            with self.assertRaises(ValueError):
                bridge.run(args)
        collector.assert_not_called()
        discover.assert_not_called()
        open_serial.assert_not_called()

    def test_empty_serial_cannot_be_used_for_direct_port_selection(self) -> None:
        with mock.patch.object(bridge.list_ports, "comports") as discover:
            with self.assertRaises(ValueError):
                bridge.select_port(None, "")
        discover.assert_not_called()

    def test_dry_run_needs_no_serial_and_does_not_discover_usb(self) -> None:
        args = bridge.parser().parse_args(["--dry-run", "--samples", "1"])
        collector = mock.Mock()
        collector.info_lines.return_value = ("pcinfo cpu Test CPU",)
        with (
            mock.patch.dict(bridge.os.environ, {}, clear=True),
            mock.patch.object(bridge, "Collector", return_value=collector),
            mock.patch.object(bridge, "stream_samples", return_value=1) as stream,
            mock.patch.object(bridge.list_ports, "comports") as discover,
            mock.patch.object(bridge, "open_serial") as open_serial,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(bridge.run(args), 0)
        stream.assert_called_once_with(collector, None, 1)
        discover.assert_not_called()
        open_serial.assert_not_called()

    def test_main_reports_missing_serial_without_hardware_access(self) -> None:
        with (
            mock.patch.dict(bridge.os.environ, {}, clear=True),
            mock.patch.object(bridge.sys, "argv", ["pc_monitor_bridge.py"]),
            mock.patch.object(bridge.os, "name", "nt"),
            mock.patch.object(bridge, "Collector") as collector,
            mock.patch.object(bridge.list_ports, "comports") as discover,
            contextlib.redirect_stderr(io.StringIO()) as stderr,
        ):
            self.assertEqual(bridge.main(), 2)
        self.assertIn("--usb-serial", stderr.getvalue())
        collector.assert_not_called()
        discover.assert_not_called()


class SerialGateTests(unittest.TestCase):
    def test_forced_port_with_wrong_serial_is_rejected_without_logging_ids(self) -> None:
        other = argparse.Namespace(
            device="COM3", vid=bridge.EXPECTED_VID,
            pid=bridge.EXPECTED_PID, serial_number="TEST-OTHER-DEVICE",
        )
        with mock.patch.object(bridge.list_ports, "comports", return_value=[other]):
            with self.assertRaises(bridge.WrongDevice) as error:
                bridge.select_port("COM3", TEST_USB_SERIAL)
        self.assertNotIn("TEST-OTHER-DEVICE", str(error.exception))
        self.assertNotIn(TEST_USB_SERIAL, str(error.exception))

    def test_exact_board_identity(self) -> None:
        board = argparse.Namespace(
            device="COM3", vid=bridge.EXPECTED_VID,
            pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL,
        )
        with mock.patch.object(bridge.list_ports, "comports", return_value=[board]):
            self.assertEqual(bridge.select_port("COM3", TEST_USB_SERIAL), "COM3")
            self.assertEqual(bridge.select_port(None, TEST_USB_SERIAL), "COM3")

    def test_other_device_on_com3_is_rejected(self) -> None:
        wrong = argparse.Namespace(device="COM3", vid=1, pid=2, serial_number="other")
        with mock.patch.object(bridge.list_ports, "comports", return_value=[wrong]):
            with self.assertRaises(bridge.WrongDevice):
                bridge.select_port("COM3", TEST_USB_SERIAL)

    def test_auto_discovery_survives_com3_to_com4(self) -> None:
        board = argparse.Namespace(
            device="COM3", vid=bridge.EXPECTED_VID,
            pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL,
        )
        moved = argparse.Namespace(
            device="COM4", vid=bridge.EXPECTED_VID,
            pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL,
        )
        with mock.patch.object(bridge.list_ports, "comports", side_effect=[[board], [moved]]):
            self.assertEqual(bridge.select_port(None, TEST_USB_SERIAL), "COM3")
            self.assertEqual(bridge.select_port(None, TEST_USB_SERIAL), "COM4")

    def test_same_display_name_without_usb_identity_is_ignored(self) -> None:
        impostor = argparse.Namespace(
            device="COM3", description="USB DeskPet", vid=bridge.EXPECTED_VID,
            pid=bridge.EXPECTED_PID, serial_number="SPOOFED",
        )
        with mock.patch.object(bridge.list_ports, "comports", return_value=[impostor]):
            with self.assertRaises(bridge.PortMissing):
                bridge.select_port(None, TEST_USB_SERIAL)

    def test_ambiguous_duplicate_identity_is_not_opened(self) -> None:
        duplicates = [
            argparse.Namespace(device=name, vid=bridge.EXPECTED_VID,
                               pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL)
            for name in ("COM3", "COM4")
        ]
        with mock.patch.object(bridge.list_ports, "comports", return_value=duplicates):
            with self.assertRaises(bridge.WrongDevice):
                bridge.select_port(None, TEST_USB_SERIAL)

    def test_old_firmware_gets_only_read_only_status(self) -> None:
        port = FakePort("deskpet-v01-20260928-r14-cat-tongue-animation")
        with self.assertRaises(bridge.WrongFirmware):
            bridge.check_firmware(port, bridge.EXPECTED_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_matching_firmware_unlocks_stream(self) -> None:
        port = FakePort(bridge.EXPECTED_VERSION)
        result = bridge.check_firmware(port, bridge.EXPECTED_VERSION)
        self.assertEqual(result["version"], bridge.EXPECTED_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_r17_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R17_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R17_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_r18_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R18_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R18_VERSION)

    def test_r19_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R19_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R19_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_r20_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R20_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R20_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_run_waits_without_telemetry_on_old_firmware(self) -> None:
        port = FakePort("deskpet-v01-20260928-r14-cat-tongue-animation")
        args = argparse.Namespace(port="COM3", usb_serial=TEST_USB_SERIAL, dry_run=False, samples=1, quiet=True)
        with (
            mock.patch.object(bridge, "Collector"),
            mock.patch.object(bridge, "select_port", return_value="COM3"),
            mock.patch.object(bridge, "open_serial", return_value=port),
            mock.patch.object(bridge.time, "sleep", side_effect=KeyboardInterrupt),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            with self.assertRaises(KeyboardInterrupt):
                bridge.run(args)
        self.assertEqual(port.written, [b"status\n"])
        self.assertFalse(port.is_open)

    def test_r21_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R21_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R21_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_r22_firmware_unlocks_stream_via_allowlist(self) -> None:
        port = FakePort(bridge.R22_VERSION)
        result = bridge.check_firmware(port, bridge.SUPPORTED_VERSIONS)
        self.assertEqual(result["version"], bridge.R22_VERSION)
        self.assertEqual(port.written, [b"status\n"])

    def test_matching_board_gets_info_then_one_sample(self) -> None:
        port = FakePort(bridge.EXPECTED_VERSION)
        collector = mock.Mock()
        collector.info_lines.return_value = (
            "pcinfo cpu Intel(R) Core(TM) i7-14700KF",
            "pcinfo gpu NVIDIA GeForce RTX 5070 Ti",
            "pcinfo cores 20 28",
        )
        collector.sample.return_value = bridge.Snapshot(5, -1, -1, 10, 35, 3365, 16303, 17, 86)
        args = argparse.Namespace(port="COM3", usb_serial=TEST_USB_SERIAL, dry_run=False, samples=1, quiet=True)
        with (
            mock.patch.object(bridge, "Collector", return_value=collector),
            mock.patch.object(bridge, "select_port", return_value="COM3"),
            mock.patch.object(bridge, "open_serial", return_value=port),
            mock.patch.object(bridge, "wait_until"),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            result = bridge.run(args)
        self.assertEqual(result, 0)
        self.assertEqual(port.written, [
            b"status\n",
            b"pcinfo cpu Intel(R) Core(TM) i7-14700KF\n",
            b"pcinfo gpu NVIDIA GeForce RTX 5070 Ti\n",
            b"pcinfo cores 20 28\n",
            b"pcmon 5 -1 -1 10 35 3365 16303 17 86\n",
        ])
        self.assertFalse(port.is_open)

    def test_unplugged_board_never_opens_serial(self) -> None:
        args = argparse.Namespace(port="COM3", usb_serial=TEST_USB_SERIAL, dry_run=False, samples=1, quiet=True)
        with (
            mock.patch.object(bridge, "Collector"),
            mock.patch.object(bridge, "select_port", side_effect=bridge.PortMissing("known board absent")),
            mock.patch.object(bridge, "open_serial") as open_serial,
            mock.patch.object(bridge.time, "sleep", side_effect=KeyboardInterrupt),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            with self.assertRaises(KeyboardInterrupt):
                bridge.run(args)
        open_serial.assert_not_called()

    def test_stream_reconnects_after_com_port_reenumeration(self) -> None:
        first = argparse.Namespace(device="COM3", vid=bridge.EXPECTED_VID,
                                   pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL)
        moved = argparse.Namespace(device="COM4", vid=bridge.EXPECTED_VID,
                                   pid=bridge.EXPECTED_PID, serial_number=TEST_USB_SERIAL)
        first_connection = FakePort(bridge.EXPECTED_VERSION)
        second_connection = FakePort(bridge.EXPECTED_VERSION)
        args = argparse.Namespace(port=None, usb_serial=TEST_USB_SERIAL, dry_run=False, samples=1, quiet=True)
        with (
            mock.patch.object(bridge, "Collector"),
            mock.patch.object(bridge.list_ports, "comports", side_effect=[[first], [moved]]),
            mock.patch.object(bridge, "open_serial", side_effect=[first_connection, second_connection]) as open_serial,
            mock.patch.object(bridge, "stream_samples", side_effect=[bridge.serial.SerialException("unplugged"), 1]),
            mock.patch.object(bridge.time, "sleep"),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            result = bridge.run(args)
        self.assertEqual(result, 0)
        self.assertEqual([call.args[0] for call in open_serial.call_args_list], ["COM3", "COM4"])
        self.assertEqual(first_connection.written[0], b"status\n")
        self.assertEqual(second_connection.written[0], b"status\n")
        self.assertFalse(first_connection.is_open)
        self.assertFalse(second_connection.is_open)


if __name__ == "__main__":
    unittest.main()
