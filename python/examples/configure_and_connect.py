"""Configure and connect to a single scan head, then print its status.

This is the Python port of ``examples/01-configure-and-connect``.

Run with::

    python configure_and_connect.py SERIAL
"""

import sys

import joescan_pinchot as js


def build_phase_table(system, head):
    """Create a basic phase table covering all phasable elements of a head."""
    t = head.type
    if t in (js.ScanHeadType.JS50X6B20, js.ScanHeadType.JS50X6B30):
        for n in range(3):
            system.phase_create()
            system.phase_insert_laser(head, js.Laser(js.Laser.L1 + n))
            system.phase_create()
            system.phase_insert_laser(head, js.Laser(js.Laser.L4 + n))
    elif t in (js.ScanHeadType.JS50Z820, js.ScanHeadType.JS50Z830):
        for n in range(4):
            system.phase_create()
            system.phase_insert_laser(head, js.Laser(js.Laser.L1 + n))
            system.phase_create()
            system.phase_insert_laser(head, js.Laser(js.Laser.L5 + n))
    elif t in (js.ScanHeadType.JS50WSC, js.ScanHeadType.JS50MX):
        system.phase_create()
        system.phase_insert_camera(head, js.Camera.A)
    elif t == js.ScanHeadType.JS50WX:
        system.phase_create()
        system.phase_insert_camera(head, js.Camera.A)
        system.phase_create()
        system.phase_insert_camera(head, js.Camera.B)
    else:
        raise RuntimeError(f"unsupported scan head type: {t.name}")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} SERIAL")
        return 1
    serial = int(sys.argv[1], 0)

    print("Pinchot API version", js.get_api_version())

    with js.ScanSystem(js.Units.INCHES) as system:
        system.discover()
        head = system.create_scan_head(serial=serial, id=0)

        major, minor, patch = head.get_firmware_version()
        print(f"scan head {head.serial}  firmware {major}.{minor}.{patch}")

        cap = head.get_capabilities()
        print("capabilities:")
        print(f"  brightness_bit_depth = {cap.camera_brightness_bit_depth}")
        print(f"  max_camera_image     = {cap.max_camera_image_width} x "
              f"{cap.max_camera_image_height}")
        print(f"  scan_period_us       = [{cap.min_scan_period_us}, "
              f"{cap.max_scan_period_us}]")
        print(f"  cameras/lasers/enc   = {cap.num_cameras}/{cap.num_lasers}/"
              f"{cap.num_encoders}")

        cfg = head.get_configuration_default()
        cfg.laser_on_time_min_us = 100
        cfg.laser_on_time_def_us = 100
        cfg.laser_on_time_max_us = 1000
        cfg.laser_detection_threshold = 120
        cfg.saturation_threshold = 800
        cfg.saturation_percentage = 30
        head.set_configuration(cfg)

        head.set_window_rectangular(30.0, -30.0, -30.0, 30.0)
        head.set_alignment(0.0, 0.0, 0.0)
        build_phase_table(system, head)

        connected = system.connect(timeout_s=10)
        if connected != system.num_scan_heads:
            print("failed to connect to all scan heads")
            return 1

        status = head.get_status()
        print("status:")
        print(f"  global_time_ns          = {status.global_time_ns}")
        print(f"  camera_a_pixels_in_window = {status.camera_a_pixels_in_window}")
        print(f"  camera_a_temp           = {status.camera_a_temp} C")

        print("min scan period is", system.get_min_scan_period(), "us")
        system.disconnect()

    return 0


if __name__ == "__main__":
    sys.exit(main())
