"""Discover every JS-50 scan head on the network and print its details.

Run with::

    python discover.py
"""

import joescan_pinchot as js


def main():
    print("Pinchot API version", js.get_api_version())

    with js.ScanSystem(js.Units.INCHES) as system:
        count = system.discover()
        print(f"Discovered {count} scan head(s):")
        for d in system.get_discovered():
            major, minor, patch = d.firmware_version
            print(f"  serial {d.serial_number}  {d.type_str:<10}  "
                  f"{d.ip_address:<15}  fw {major}.{minor}.{patch}  "
                  f"link {d.link_speed_mbps} Mbps  state {d.state.name}")


if __name__ == "__main__":
    main()
