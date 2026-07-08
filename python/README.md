# JoeScan Pinchot — Python Wrapper

A Python wrapper for the JoeScan Pinchot C API, used to control JS-50 scan
heads. It binds the native `pinchot` shared library with `ctypes` (no compiler
or build step required at install time) and layers a Pythonic, object-oriented
interface on top.

Wraps Pinchot API **v16.3.2**.

## Installation

The wrapper is pure Python; only the native `pinchot` shared library is
platform-specific (`pinchot.dll` on Windows, `libpinchot.so` on Linux,
`libpinchot.dylib` on macOS). **The native library is not committed to this
repository** — you build it yourself and the wrapper locates it, in order, from:

1. an explicit path passed to `joescan_pinchot.initialize(path)`;
2. the `PINCHOT_LIB_DIR` environment variable (a directory);
3. a `lib/` directory inside the installed package
   (`joescan_pinchot/lib/`) — drop a built library there if you like;
4. the repository `build/Release`, `build/Debug`, `build`, `build-ninja`
   directories (whatever CMake produced);
5. the system loader search path (`PATH` / `LD_LIBRARY_PATH`).

### Install the wrapper

```sh
cd python
pip install -e .
```

### Build the native library

**Linux** (single-config generator emits `build/libpinchot.so`):

```sh
cd <repo root>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
export PINCHOT_LIB_DIR="$PWD/build"      # dir containing libpinchot.so
```

**Windows** (multi-config generator emits `build/Release/pinchot.dll`):

```powershell
cmake -S . -B build
cmake --build build --config Release
$env:PINCHOT_LIB_DIR = "$PWD\build\Release"
```

Instead of the environment variable you can copy the built library into
`python/joescan_pinchot/lib/` (git-ignored), or pass its path directly:

```python
import joescan_pinchot as js
js.initialize("/path/to/libpinchot.so")   # or r"C:\path\to\pinchot.dll"
print(js.get_api_version())
```

If the library can't be found on import, `import joescan_pinchot` still
succeeds but `joescan_pinchot.load_error` holds the `OSError` explaining what
was tried; call `js.initialize(...)` to load it explicitly.

## Quick start

```python
import joescan_pinchot as js

with js.ScanSystem(js.Units.INCHES) as system:
    system.discover()
    for d in system.get_discovered():
        print(d)

    head = system.create_scan_head(serial=23185, id=0)

    cfg = head.get_configuration_default()
    cfg.laser_on_time_min_us = 100
    cfg.laser_on_time_def_us = 100
    cfg.laser_on_time_max_us = 1000
    head.set_configuration(cfg)
    head.set_window_rectangular(30.0, -30.0, -30.0, 30.0)
    head.set_alignment(0.0, 0.0, 0.0)

    # JS50WSC is camera-driven: one phase using camera A.
    system.phase_create()
    system.phase_insert_camera(head, js.Camera.A)

    system.connect(timeout_s=5)
    period = max(system.get_min_scan_period(), 1000)
    system.start_scanning(period, js.DataFormat.XY_BRIGHTNESS_FULL)

    head.wait_until_profiles_available(10, timeout_us=1_000_000)
    for profile in head.get_profiles():
        for point in profile.data:            # only valid points
            x, y, b = point.x, point.y, point.brightness  # x/y in system units
        print(profile.sequence_number, len(profile.data), "points")

    system.stop_scanning()
    system.disconnect()
```

## Design

The package has two layers:

- **High-level API** (`ScanSystem`, `ScanHead`, `Profile`, …) — idiomatic
  Python. Every call that returns a negative `jsError` raises
  `PinchotError`, which carries the numeric `.code` and, when available, the
  extended per-call error string (`.extended`).
- **Low-level binding** (`joescan_pinchot._native`) — the raw `ctypes` structs
  and function prototypes mapped one-to-one from `joescan_pinchot.h`. The loaded
  library handle is `joescan_pinchot._native.lib`. Use this if you need a struct
  or function the high-level layer doesn't expose.

### Key mappings

| C | Python |
|---|--------|
| `jsScanSystemCreate` / `Free` | `ScanSystem(units)` / `.free()` (or a `with` block) |
| `jsScanSystemDiscover` / `GetDiscovered` | `system.discover()` / `system.get_discovered()` |
| `jsScanSystemCreateScanHead` | `system.create_scan_head(serial, id)` |
| `jsScanSystemConnect` / `Disconnect` | `system.connect(timeout_s)` / `system.disconnect()` |
| `jsScanSystemStartScanning` | `system.start_scanning(period_us, fmt)` |
| `jsScanHeadGetProfiles` | `head.get_profiles(max_profiles)` → `list[Profile]` |
| `jsScanHeadSetConfiguration` | `head.set_configuration(cfg)` |
| `jsScanHeadSetWindowRectangular` | `head.set_window_rectangular(top, bottom, left, right)` |
| `jsScanHeadGetDiagnosticImage` | `head.get_diagnostic_image(camera, laser, ...)` → `CameraImage` |
| `enum jsUnits`, `jsCamera`, … | `js.Units`, `js.Camera`, … (`IntEnum`) |

Camera/laser-specific variants (`...Camera` / `...Laser`) are folded into single
methods with optional `camera=` / `laser=` keyword arguments, e.g.
`head.set_window_rectangular(..., camera=js.Camera.A)`.

`Profile.data` returns only the *valid* measurement points as `ProfilePoint`
objects, with `x`/`y` already rescaled from the raw 1/1000 units into
scan-system units. The point data is copied out of the native buffer, so
`Profile` objects remain valid after the next `get_profiles()` call. Use
`head.get_raw_profiles()` for the untouched `jsRawProfile` structs.

Diagnostic image captures (`head.get_diagnostic_image[_camera|_laser](...)`)
return a `CameraImage` whose `.pixels` attribute holds the raw 8-bit grayscale
data and whose `.to_bmp()` / `.save(path)` methods produce a standard BMP
bitmap — no imaging library required.

## Examples

See the [`examples/`](examples) directory:

- `discover.py` — list all scan heads on the network
- `configure_and_connect.py SERIAL` — configure, connect, print status
- `basic_scanning.py SERIAL [SECONDS]` — scan and summarize the profiles read

## Supported platforms

Windows and Linux (and macOS if a `libpinchot.dylib` is available). Requires
Python 3.7+.

The binding is the same on every platform: all structs are byte-packed
(`#pragma pack(1)`) so there is no platform-dependent padding, `bool` is one
byte and enums are four bytes on both toolchains, and the native functions use
the standard C calling convention on each (`__cdecl` on Windows, the SysV ABI on
Linux). Only the shared-library file name and the build command differ.
