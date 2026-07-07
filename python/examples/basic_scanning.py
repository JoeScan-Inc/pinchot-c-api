"""Connect to a scan head, scan for a moment, and report on the profiles read.

Run with::

    python basic_scanning.py SERIAL [DURATION_SECONDS]
"""

import sys
import time

import joescan_pinchot as js

from configure_and_connect import build_phase_table


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} SERIAL [DURATION_SECONDS]")
        return 1
    serial = int(sys.argv[1], 0)
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0

    with js.ScanSystem(js.Units.INCHES) as system:
        system.discover()
        head = system.create_scan_head(serial=serial, id=0)

        cfg = head.get_configuration_default()
        cfg.laser_on_time_min_us = 100
        cfg.laser_on_time_def_us = 100
        cfg.laser_on_time_max_us = 1000
        head.set_configuration(cfg)
        head.set_window_rectangular(30.0, -30.0, -30.0, 30.0)
        head.set_alignment(0.0, 0.0, 0.0)
        build_phase_table(system, head)

        system.connect(timeout_s=10)
        period = max(system.get_min_scan_period(), 1000)
        print(f"scanning at {period} us for {duration} s ...")
        system.start_scanning(period, js.DataFormat.XY_BRIGHTNESS_FULL)

        total = 0
        min_pts = None
        max_pts = 0
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            head.wait_until_profiles_available(
                js.JS_SCAN_HEAD_PROFILES_MAX, timeout_us=100_000)
            for profile in head.get_profiles():
                total += 1
                n = len(profile.data)
                max_pts = max(max_pts, n)
                min_pts = n if min_pts is None else min(min_pts, n)

        system.stop_scanning()
        system.disconnect()

        rate = total / duration if duration else 0
        print(f"read {total} profiles ({rate:.0f}/s), "
              f"valid points per profile: min={min_pts} max={max_pts}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
