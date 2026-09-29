"""Build and run offline regression tests; never opens a serial port."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tools" / "tests"
CPP_TESTS = (
    "esp_provision_r19_test", "esp_message_protocol_r17_test",
    "pc_monitor_protocol_r16_test", "touch_gesture_r8_test",
    "touch_gesture_r15_test", "cat_touch_model_r15_test",
    "wifi_network_store_r20_test", "esp_nine_key_r20_test",
    "esp_wifi_ui_r20_test", "esp_card_transfer_r21_test",
    "cat_touch_decay_r22_test",
)


def msvc_environment(vcvars: Path) -> dict[str, str]:
    if os.name != "nt":
        raise ValueError("--vcvars is only supported on Windows")
    if not vcvars.is_file():
        raise ValueError(f"MSVC setup script not found: {vcvars}")
    if any(character in str(vcvars) for character in ('"', '\r', '\n')):
        raise ValueError("Invalid characters in MSVC setup path")
    # A batch script cannot mutate this Python process; import its resulting env.
    result = subprocess.run(
        f'cmd.exe /d /s /c ""{vcvars.resolve()}" >nul && set"',
        check=True, capture_output=True, text=True, errors="replace",
    )
    environment = dict(os.environ)
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            environment[key.upper()] = value
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="C++ compiler executable (cl, g++, or clang++)")
    parser.add_argument("--vcvars", type=Path, help="Optional path to MSVC vcvars64.bat")
    parser.add_argument("--legacy-shake", action="store_true", help="Also run the historical R7 source-shape check (not compatible with R22 role dispatch)")
    parser.add_argument("--sage-source", type=Path, help="Optional original Sage V03 device directory for exact-pixel regression")
    args = parser.parse_args()
    environment = msvc_environment(args.vcvars) if args.vcvars else dict(os.environ)
    compiler = args.compiler
    if not compiler:
        compiler = next((candidate for candidate in ("cl", "g++", "clang++")
                         if shutil.which(candidate, path=environment.get("PATH"))), None)
    if not compiler:
        raise ValueError("No C++ compiler found. Use --compiler or --vcvars.")
    compiler = shutil.which(compiler, path=environment.get("PATH")) or compiler
    msvc = Path(compiler).stem.lower() in ("cl", "clang-cl")
    build = ROOT / "build" / "host-tests"
    build.mkdir(parents=True, exist_ok=True)
    tests = list(CPP_TESTS)
    if args.legacy_shake:
        tests.append("shake_detector_r7_test")
    if args.sage_source:
        tests.append("sage_assets_r8_test")
    for name in tests:
        include = TESTS / ("cat_render_stubs" if name == "cat_touch_decay_r22_test" else "stubs")
        sources = [TESTS / f"{name}.cpp"]
        if name == "cat_touch_decay_r22_test":
            sources.append(ROOT / "sketch" / "DeskPetV01" / "BlackCatPet.cpp")
        if name == "sage_assets_r8_test":
            sources.append(ROOT / "sketch" / "DeskPetV01" / "SageAssets.cpp")
        executable = build / (name + (".exe" if os.name == "nt" else ""))
        if msvc:
            command = [compiler, "/nologo", "/EHsc", "/std:c++17", "/utf-8",
                       f"/I{include}", f"/Fe{executable}", *map(str, sources)]
        else:
            command = [compiler, "-std=c++17", "-I", str(include), *map(str, sources), "-o", str(executable)]
        print(f"BUILD {name}", flush=True)
        subprocess.run(command, cwd=build, env=environment, check=True)
        arguments: list[str] = []
        if name == "shake_detector_r7_test":
            arguments.append(str(ROOT / "sketch" / "DeskPetV01" / "DeskPetV01.ino"))
        elif name == "sage_assets_r8_test":
            arguments.append(str(args.sage_source.resolve()))
        subprocess.run([str(executable), *arguments], cwd=ROOT, env=environment, check=True)
        print(f"PASS {name}", flush=True)
    subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", str(TESTS), "-p", "test_pc_monitor_bridge.py"],
        cwd=ROOT, env=environment, check=True,
    )
    print(f"PASS: {len(tests)} C++ programs and PC monitor Python suite. No serial port opened.")
    if not args.sage_source:
        print("SKIP: optional Sage source-pixel equality test (original source frames not bundled).")
    if not args.legacy_shake:
        print("SKIP: historical R7 source-shape test (its single-role assertion predates R22 role dispatch).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
