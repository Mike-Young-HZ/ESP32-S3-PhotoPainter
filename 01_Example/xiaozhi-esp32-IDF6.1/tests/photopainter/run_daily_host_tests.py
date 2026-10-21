#!/usr/bin/env python3
"""Compile and run the daily-photo pure functions with clang++."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "user_app_bsp"
IMPL = COMPONENT / "daily_photo.cpp"
TEST_SRC = Path(__file__).resolve().parent / "test_daily_photo.cpp"
SAMPLE = ROOT.parent.parent / "02_SDCARD" / "07_daily_photo" / "daily.json"
EXPECTED_SAMPLE = (
    b'{"host":"127.0.0.1:8765","key":"yourdownloadkey","hour":8,"tz_offset_min":480}'
)


def main() -> int:
    if not SAMPLE.is_file():
        print(f"missing daily.json sample: {SAMPLE}", file=sys.stderr)
        return 1
    raw = SAMPLE.read_bytes()
    if raw not in (EXPECTED_SAMPLE, EXPECTED_SAMPLE + b"\n"):
        print(f"daily.json must contain only the placeholder config: {SAMPLE}", file=sys.stderr)
        return 1

    # Prefer a temp directory so the binary is not left in the source tree.
    # migration/host-tests is gitignored and is only a fallback.
    try:
        temp_parent = Path(tempfile.gettempdir())
        temp_parent.mkdir(parents=True, exist_ok=True)
        probe = temp_parent / ".daily-photo-host-write-probe"
        probe.write_text("", encoding="utf-8")
        probe.unlink()
        base = temp_parent
    except OSError:
        base = ROOT / "migration" / "host-tests"
        base.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="daily-photo-host-", dir=base) as temp:
        binary = Path(temp) / "test_daily_photo"
        command = [
            "clang++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fno-exceptions",
            "-I",
            str(COMPONENT),
            str(IMPL),
            str(TEST_SRC),
            "-o",
            str(binary),
        ]
        print(" ".join(command), flush=True)
        compiled = subprocess.run(command)
        if compiled.returncode != 0:
            return compiled.returncode or 1
        ran = subprocess.run([str(binary), str(SAMPLE)])
        return ran.returncode or 0


if __name__ == "__main__":
    if shutil.which("clang++") is None:
        print("clang++ was not found", file=sys.stderr)
        sys.exit(1)
    sys.exit(main())
