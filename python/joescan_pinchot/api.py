"""Pythonic high-level interface to the Pinchot API.

The two central classes are :class:`ScanSystem` and :class:`ScanHead`.  A typical
session looks like::

    from joescan_pinchot import ScanSystem, Units, DataFormat

    with ScanSystem(Units.INCHES) as system:
        system.discover()
        head = system.create_scan_head(serial=1234, id=0)
        head.set_configuration(head.get_configuration_default())
        head.set_window_rectangular(30.0, -30.0, -30.0, 30.0)
        system.phase_create()
        system.phase_insert_camera(head, Camera.A)
        system.connect(timeout_s=5)
        system.start_scanning(period_us=1000, fmt=DataFormat.XY_BRIGHTNESS_FULL)
        head.wait_until_profiles_available(1, timeout_us=1_000_000)
        for profile in head.get_profiles():
            print(len(profile.data), "points")
        system.stop_scanning()

Every method translates a negative ``jsError`` return value into a
:class:`~joescan_pinchot.exceptions.PinchotError`.
"""

import ctypes

from . import _native as _n
from .enums import (
    JS_SCAN_HEAD_PROFILES_MAX,
    CableOrientation,
    Camera,
    DataFormat,
    DiagnosticMode,
    Error,
    Laser,
    ScanHeadState,
    ScanHeadType,
    ScanWindowType,
    Units,
)
from .exceptions import PinchotError

__all__ = [
    "ScanSystem",
    "ScanHead",
    "Profile",
    "ProfilePoint",
    "Discovered",
    "ScanSyncDiscovered",
    "get_api_version",
    "get_api_semantic_version",
    "error_to_string",
    "power_cycle_scan_head",
]


# --------------------------------------------------------------------------
# Module-level helpers
# --------------------------------------------------------------------------

def get_api_version():
    """Return the native API version string, e.g. ``"16.3.2"``."""
    s = ctypes.c_char_p()
    _n.lib.jsGetAPIVersion(ctypes.byref(s))
    return s.value.decode() if s.value else ""


def get_api_semantic_version():
    """Return the native API version as a ``(major, minor, patch)`` tuple."""
    major = ctypes.c_uint32()
    minor = ctypes.c_uint32()
    patch = ctypes.c_uint32()
    _n.lib.jsGetAPISemanticVersion(
        ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch)
    )
    return (major.value, minor.value, patch.value)


def error_to_string(code):
    """Convert a ``jsError`` code into its human-readable string."""
    s = ctypes.c_char_p()
    _n.lib.jsGetError(int(code), ctypes.byref(s))
    return s.value.decode() if s.value else ""


def power_cycle_scan_head(serial_number):
    """Remotely soft power-cycle a (disconnected) scan head by serial number."""
    r = _n.lib.jsPowerCycleScanHead(int(serial_number))
    _check(r, msg="failed to power cycle scan head")


def _check(code, scan_system=None, scan_head=None, msg="Pinchot API error"):
    """Raise :class:`PinchotError` if ``code`` is a negative ``jsError``."""
    if code is not None and code < 0:
        extended = None
        try:
            s = ctypes.c_char_p()
            if scan_head is not None:
                _n.lib.jsScanHeadGetLastErrorExtended(scan_head, ctypes.byref(s))
            elif scan_system is not None:
                _n.lib.jsScanSystemGetLastErrorExtended(
                    scan_system, ctypes.byref(s)
                )
            if s.value:
                extended = s.value.decode(errors="replace")
        except Exception:  # pragma: no cover - defensive
            extended = None
        raise PinchotError(msg, code=code, extended=extended)
    return code


# --------------------------------------------------------------------------
# Value objects
# --------------------------------------------------------------------------

def _ip_to_str(ip):
    """Render a ``uint32`` IPv4 address (host byte order) as dotted quad."""
    return ".".join(str((ip >> (8 * i)) & 0xFF) for i in (3, 2, 1, 0))


class Discovered:
    """Information about a scan head found on the network."""

    __slots__ = (
        "serial_number", "type", "type_str",
        "firmware_version", "ip_address", "client_name",
        "client_ip_address", "client_netmask", "link_speed_mbps", "state",
    )

    def __init__(self, d):
        self.serial_number = d.serial_number
        self.type = ScanHeadType(d.type)
        self.type_str = d.type_str.decode(errors="replace")
        self.firmware_version = (
            d.firmware_version_major,
            d.firmware_version_minor,
            d.firmware_version_patch,
        )
        self.ip_address = _ip_to_str(d.ip_addr)
        self.client_name = d.client_name_str.decode(errors="replace")
        self.client_ip_address = _ip_to_str(d.client_ip_addr)
        self.client_netmask = _ip_to_str(d.client_netmask)
        self.link_speed_mbps = d.link_speed_mbps
        self.state = ScanHeadState(d.state)

    def __repr__(self):
        return (
            f"Discovered(serial_number={self.serial_number}, "
            f"type={self.type.name}, ip_address={self.ip_address!r}, "
            f"firmware_version={self.firmware_version})"
        )


class ScanSyncDiscovered:
    """Information about a ScanSync found on the network."""

    __slots__ = ("serial_number", "firmware_version", "ip_address")

    def __init__(self, d):
        self.serial_number = d.serial_number
        self.firmware_version = (
            d.firmware_version_major,
            d.firmware_version_minor,
            d.firmware_version_patch,
        )
        self.ip_address = _ip_to_str(d.ip_addr)

    def __repr__(self):
        return (
            f"ScanSyncDiscovered(serial_number={self.serial_number}, "
            f"ip_address={self.ip_address!r})"
        )


class ProfilePoint:
    """A single measured point within a profile.

    ``x`` and ``y`` are in scan-system units (the raw API reports 1/1000 units;
    this class rescales them).  ``brightness`` is the raw measured value.
    """

    __slots__ = ("x", "y", "brightness")

    def __init__(self, x, y, brightness):
        self.x = x
        self.y = y
        self.brightness = brightness

    def __repr__(self):
        return f"ProfilePoint(x={self.x}, y={self.y}, brightness={self.brightness})"


class Profile:
    """A processed profile copied out of a native ``jsProfile`` struct.

    Scalar header fields are copied eagerly.  The list of valid measurement
    points is built lazily on first access to :attr:`data`.
    """

    __slots__ = (
        "scan_head_id", "camera", "laser", "timestamp_ns", "flags",
        "sequence_number", "encoder_values", "laser_on_time_us", "format",
        "_raw_data", "_data_len", "_data",
    )

    def __init__(self, p):
        self.scan_head_id = p.scan_head_id
        self.camera = Camera(p.camera)
        self.laser = Laser(p.laser)
        self.timestamp_ns = p.timestamp_ns
        self.flags = p.flags
        self.sequence_number = p.sequence_number
        n = p.num_encoder_values
        self.encoder_values = [p.encoder_values[i] for i in range(n)]
        self.laser_on_time_us = p.laser_on_time_us
        self.format = DataFormat(p.format)
        self._data_len = p.data_len
        # Copy the raw point array out of the reusable native buffer so this
        # object stays valid after the buffer is reused/freed.
        self._raw_data = bytes(
            ctypes.string_at(
                ctypes.addressof(p.data),
                p.data_len * ctypes.sizeof(_n.jsProfileData),
            )
        )
        self._data = None

    @property
    def data(self):
        """List of valid :class:`ProfilePoint` objects (x/y in system units)."""
        if self._data is None:
            arr = (_n.jsProfileData * self._data_len).from_buffer_copy(
                self._raw_data
            )
            invalid = -2147483648
            pts = []
            for d in arr:
                if d.x == invalid and d.y == invalid:
                    continue
                pts.append(ProfilePoint(d.x / 1000.0, d.y / 1000.0, d.brightness))
            self._data = pts
        return self._data

    def __len__(self):
        return len(self.data)

    def __repr__(self):
        return (
            f"Profile(scan_head_id={self.scan_head_id}, "
            f"camera={self.camera.name}, laser={self.laser.name}, "
            f"sequence_number={self.sequence_number}, "
            f"points={self._data_len})"
        )


# --------------------------------------------------------------------------
# ScanSystem
# --------------------------------------------------------------------------

class ScanSystem:
    """Manages and coordinates a group of :class:`ScanHead` objects."""

    def __init__(self, units=Units.INCHES):
        handle = _n.lib.jsScanSystemCreate(int(units))
        if handle < 0:
            raise PinchotError(
                "failed to create scan system", code=int(handle)
            )
        self._handle = handle
        self._heads = {}

    # -- lifecycle --------------------------------------------------------

    @property
    def handle(self):
        """The raw native ``jsScanSystem`` token."""
        return self._handle

    def free(self):
        """Free the scan system and all scan heads it owns."""
        if self._handle is not None:
            _n.lib.jsScanSystemFree(self._handle)
            self._handle = None
            self._heads.clear()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.free()
        return False

    def __del__(self):
        try:
            self.free()
        except Exception:  # pragma: no cover - interpreter shutdown
            pass

    # -- discovery --------------------------------------------------------

    def discover(self):
        """Discover scan heads on the network; returns the number found."""
        r = _n.lib.jsScanSystemDiscover(self._handle)
        return _check(r, scan_system=self._handle, msg="discovery failed")

    def get_discovered(self, max_results=None):
        """Return a list of :class:`Discovered` scan heads from a prior discover."""
        if max_results is None:
            max_results = self.discover()
        if max_results <= 0:
            return []
        arr = (_n.jsDiscovered * max_results)()
        r = _n.lib.jsScanSystemGetDiscovered(self._handle, arr, max_results)
        _check(r, scan_system=self._handle, msg="failed to get discovered")
        return [Discovered(arr[i]) for i in range(r)]

    def scan_sync_discover(self):
        """Discover ScanSyncs on the network; returns the number found."""
        r = _n.lib.jsScanSystemScanSyncDiscover(self._handle)
        return _check(r, scan_system=self._handle, msg="ScanSync discovery failed")

    def get_scan_sync_discovered(self, max_results=None):
        """Return a list of :class:`ScanSyncDiscovered` from a prior discover."""
        if max_results is None:
            max_results = self.scan_sync_discover()
        if max_results <= 0:
            return []
        arr = (_n.jsScanSyncDiscovered * max_results)()
        r = _n.lib.jsScanSystemGetScanSyncDiscovered(
            self._handle, arr, max_results
        )
        _check(r, scan_system=self._handle,
               msg="failed to get discovered ScanSyncs")
        return [ScanSyncDiscovered(arr[i]) for i in range(r)]

    # -- ScanSync encoder mapping ----------------------------------------

    def set_scan_sync_encoder(self, serial_main, serial_aux1=0, serial_aux2=0):
        """Assign ScanSync serials to the main/aux1/aux2 encoder indices."""
        r = _n.lib.jsScanSystemSetScanSyncEncoder(
            self._handle, int(serial_main), int(serial_aux1), int(serial_aux2)
        )
        _check(r, scan_system=self._handle, msg="failed to set ScanSync encoder")

    def get_scan_sync_encoder(self):
        """Return the ``(main, aux1, aux2)`` ScanSync serials for the encoders."""
        main = ctypes.c_uint32()
        aux1 = ctypes.c_uint32()
        aux2 = ctypes.c_uint32()
        r = _n.lib.jsScanSystemGetScanSyncEncoder(
            self._handle, ctypes.byref(main), ctypes.byref(aux1),
            ctypes.byref(aux2)
        )
        _check(r, scan_system=self._handle, msg="failed to get ScanSync encoder")
        return (main.value, aux1.value, aux2.value)

    def set_default_scan_sync_encoder(self):
        """Clear ScanSync mapping and use the default (lowest serial is MAIN)."""
        r = _n.lib.jsScanSystemSetDefaultScanSyncEncoder(self._handle)
        _check(r, scan_system=self._handle,
               msg="failed to set default ScanSync encoder")

    def get_scan_sync_status(self, serial):
        """Return the :class:`~joescan_pinchot._native.jsScanSyncStatus` struct."""
        status = _n.jsScanSyncStatus()
        r = _n.lib.jsScanSystemGetScanSyncStatus(
            self._handle, int(serial), ctypes.byref(status)
        )
        _check(r, scan_system=self._handle, msg="failed to get ScanSync status")
        return status

    # -- scan head creation ----------------------------------------------

    def create_scan_head(self, serial, id):
        """Create a :class:`ScanHead` for the given serial number and user id."""
        handle = _n.lib.jsScanSystemCreateScanHead(
            self._handle, int(serial), int(id)
        )
        if handle < 0:
            _check(int(handle), scan_system=self._handle,
                   msg="failed to create scan head")
        head = ScanHead(self, handle)
        self._heads[handle] = head
        return head

    def get_scan_head_by_id(self, id):
        """Return the :class:`ScanHead` previously created with ``id``."""
        handle = _n.lib.jsScanSystemGetScanHeadById(self._handle, int(id))
        if handle < 0:
            _check(int(handle), scan_system=self._handle,
                   msg="failed to get scan head by id")
        return self._heads.get(handle) or ScanHead(self, handle)

    def get_scan_head_by_serial(self, serial):
        """Return the :class:`ScanHead` previously created with ``serial``."""
        handle = _n.lib.jsScanSystemGetScanHeadBySerial(self._handle, int(serial))
        if handle < 0:
            _check(int(handle), scan_system=self._handle,
                   msg="failed to get scan head by serial")
        return self._heads.get(handle) or ScanHead(self, handle)

    @property
    def num_scan_heads(self):
        """Number of scan heads that belong to this system."""
        r = _n.lib.jsScanSystemGetNumberScanHeads(self._handle)
        return _check(r, scan_system=self._handle,
                      msg="failed to get number of scan heads")

    @property
    def scan_heads(self):
        """List of :class:`ScanHead` objects created through this system."""
        return list(self._heads.values())

    # -- connection -------------------------------------------------------

    def connect(self, timeout_s=10):
        """Connect to all scan heads; returns the number connected."""
        r = _n.lib.jsScanSystemConnect(self._handle, int(timeout_s))
        return _check(r, scan_system=self._handle, msg="failed to connect")

    def disconnect(self):
        """Disconnect all scan heads."""
        r = _n.lib.jsScanSystemDisconnect(self._handle)
        _check(r, scan_system=self._handle, msg="failed to disconnect")

    @property
    def is_connected(self):
        """``True`` if every scan head in the system is connected."""
        return bool(_n.lib.jsScanSystemIsConnected(self._handle))

    # -- phase table ------------------------------------------------------

    def phase_clear_all(self):
        """Clear all phases previously created."""
        r = _n.lib.jsScanSystemPhaseClearAll(self._handle)
        _check(r, scan_system=self._handle, msg="failed to clear phases")

    def phase_create(self):
        """Create a new phase entry, appended after the last one."""
        r = _n.lib.jsScanSystemPhaseCreate(self._handle)
        _check(r, scan_system=self._handle, msg="failed to create phase")

    def phase_insert_camera(self, scan_head, camera, config=None):
        """Insert a camera-driven scan head into the current phase.

        If ``config`` (a :class:`~joescan_pinchot._native.jsScanHeadConfiguration`)
        is given, the per-phase configuration variant is used.
        """
        if config is None:
            r = _n.lib.jsScanSystemPhaseInsertCamera(
                self._handle, scan_head._handle, int(camera)
            )
        else:
            r = _n.lib.jsScanSystemPhaseInsertConfigurationCamera(
                self._handle, scan_head._handle, ctypes.byref(config),
                int(camera)
            )
        _check(r, scan_system=self._handle, msg="failed to insert camera phase")

    def phase_insert_laser(self, scan_head, laser, config=None):
        """Insert a laser-driven scan head into the current phase.

        If ``config`` is given, the per-phase configuration variant is used.
        """
        if config is None:
            r = _n.lib.jsScanSystemPhaseInsertLaser(
                self._handle, scan_head._handle, int(laser)
            )
        else:
            r = _n.lib.jsScanSystemPhaseInsertConfigurationLaser(
                self._handle, scan_head._handle, ctypes.byref(config),
                int(laser)
            )
        _check(r, scan_system=self._handle, msg="failed to insert laser phase")

    # -- configure / scan period -----------------------------------------

    def get_min_scan_period(self):
        """Return the minimum achievable scan period in microseconds."""
        r = _n.lib.jsScanSystemGetMinScanPeriod(self._handle)
        return _check(r, scan_system=self._handle,
                      msg="failed to get min scan period")

    def configure(self):
        """Send configuration to scan heads ahead of scanning."""
        r = _n.lib.jsScanSystemConfigure(self._handle)
        return _check(r, scan_system=self._handle, msg="failed to configure")

    @property
    def is_configured(self):
        """``True`` if the system is configured and can start scanning fast."""
        return bool(_n.lib.jsScanSystemIsConfigured(self._handle))

    # -- profile scanning -------------------------------------------------

    def start_scanning(self, period_us, fmt=DataFormat.XY_BRIGHTNESS_FULL):
        """Begin scanning at ``period_us`` microseconds with data format ``fmt``."""
        r = _n.lib.jsScanSystemStartScanning(
            self._handle, int(period_us), int(fmt)
        )
        _check(r, scan_system=self._handle, msg="failed to start scanning")

    def stop_scanning(self):
        """Stop scanning."""
        r = _n.lib.jsScanSystemStopScanning(self._handle)
        _check(r, scan_system=self._handle, msg="failed to stop scanning")

    @property
    def is_scanning(self):
        """``True`` while the system is scanning."""
        return bool(_n.lib.jsScanSystemIsScanning(self._handle))

    # -- frame scanning ---------------------------------------------------

    def start_frame_scanning(self, period_us, fmt=DataFormat.XY_BRIGHTNESS_FULL):
        """Begin frame scanning at ``period_us`` with data format ``fmt``."""
        r = _n.lib.jsScanSystemStartFrameScanning(
            self._handle, int(period_us), int(fmt)
        )
        _check(r, scan_system=self._handle, msg="failed to start frame scanning")

    def get_profiles_per_frame(self):
        """Number of profiles that make up a single frame."""
        r = _n.lib.jsScanSystemGetProfilesPerFrame(self._handle)
        return _check(r, scan_system=self._handle,
                      msg="failed to get profiles per frame")

    def wait_until_frame_available(self, timeout_us):
        """Block up to ``timeout_us`` for a frame; returns frames available."""
        r = _n.lib.jsScanSystemWaitUntilFrameAvailable(
            self._handle, int(timeout_us)
        )
        return _check(r, scan_system=self._handle,
                      msg="failed waiting for frame")

    @property
    def is_frame_available(self):
        """``True`` if a full frame can be constructed."""
        return bool(_n.lib.jsScanSystemIsFrameAvailable(self._handle))

    def clear_frames(self):
        """Empty the client-side frame buffers."""
        r = _n.lib.jsScanSystemClearFrames(self._handle)
        _check(r, scan_system=self._handle, msg="failed to clear frames")

    def get_frame(self):
        """Read one frame; returns a list of :class:`Profile` (invalid slots dropped)."""
        count = self.get_profiles_per_frame()
        arr = (_n.jsProfile * count)()
        r = _n.lib.jsScanSystemGetFrame(self._handle, arr)
        _check(r, scan_system=self._handle, msg="failed to get frame")
        out = []
        for i in range(count):
            p = arr[i]
            if p.timestamp_ns != 0 and p.format != int(DataFormat.INVALID):
                out.append(Profile(p))
        return out

    def get_raw_frame(self):
        """Read one frame of raw ``jsRawProfile`` structs (returned as a list)."""
        count = self.get_profiles_per_frame()
        arr = (_n.jsRawProfile * count)()
        r = _n.lib.jsScanSystemGetRawFrame(self._handle, arr)
        _check(r, scan_system=self._handle, msg="failed to get raw frame")
        return [arr[i] for i in range(count)]

    # -- idle scanning ----------------------------------------------------

    def set_idle_scan_period(self, idle_period_us):
        """Set the idle scan period (0 disables the laser while idle)."""
        r = _n.lib.jsScanSystemSetIdleScanPeriod(self._handle, int(idle_period_us))
        _check(r, scan_system=self._handle, msg="failed to set idle scan period")

    def get_idle_scan_period(self):
        """Return the configured idle scan period in microseconds."""
        v = ctypes.c_uint32()
        r = _n.lib.jsScanSystemGetIdleScanPeriod(self._handle, ctypes.byref(v))
        _check(r, scan_system=self._handle, msg="failed to get idle scan period")
        return v.value

    def disable_idle_scanning(self):
        """Disable idle scanning."""
        r = _n.lib.jsScanSystemDisableIdleScanning(self._handle)
        _check(r, scan_system=self._handle, msg="failed to disable idle scanning")

    @property
    def is_idle_scanning_enabled(self):
        """``True`` if idle scanning is enabled."""
        return bool(_n.lib.jsScanSystemIsIdleScanningEnabled(self._handle))


# --------------------------------------------------------------------------
# ScanHead
# --------------------------------------------------------------------------

class ScanHead:
    """Represents a single physical scan head within a :class:`ScanSystem`."""

    def __init__(self, system, handle):
        self._system = system
        self._handle = handle

    @property
    def handle(self):
        """The raw native ``jsScanHead`` token."""
        return self._handle

    def _check(self, code, msg):
        return _check(code, scan_head=self._handle, msg=msg)

    # -- identity / info --------------------------------------------------

    @property
    def type(self):
        """The :class:`~joescan_pinchot.enums.ScanHeadType` (valid once connected)."""
        return ScanHeadType(_n.lib.jsScanHeadGetType(self._handle))

    @property
    def id(self):
        """The user-assigned numeric id."""
        return _n.lib.jsScanHeadGetId(self._handle)

    @property
    def serial(self):
        """The physical serial number."""
        return _n.lib.jsScanHeadGetSerial(self._handle)

    @property
    def is_connected(self):
        """``True`` while this scan head is connected."""
        return bool(_n.lib.jsScanHeadIsConnected(self._handle))

    def get_capabilities(self):
        """Return the ``jsScanHeadCapabilities`` struct for this scan head."""
        cap = _n.jsScanHeadCapabilities()
        r = _n.lib.jsScanHeadGetCapabilities(self._handle, ctypes.byref(cap))
        self._check(r, "failed to get capabilities")
        return cap

    def get_firmware_version(self):
        """Return the firmware version as a ``(major, minor, patch)`` tuple."""
        major = ctypes.c_uint32()
        minor = ctypes.c_uint32()
        patch = ctypes.c_uint32()
        r = _n.lib.jsScanHeadGetFirmwareVersion(
            self._handle, ctypes.byref(major), ctypes.byref(minor),
            ctypes.byref(patch)
        )
        self._check(r, "failed to get firmware version")
        return (major.value, minor.value, patch.value)

    # -- configuration ----------------------------------------------------

    def set_configuration(self, config):
        """Apply a ``jsScanHeadConfiguration`` to this scan head."""
        r = _n.lib.jsScanHeadSetConfiguration(self._handle, ctypes.byref(config))
        self._check(r, "failed to set configuration")

    def get_configuration(self):
        """Return the currently applied ``jsScanHeadConfiguration``."""
        cfg = _n.jsScanHeadConfiguration()
        r = _n.lib.jsScanHeadGetConfiguration(self._handle, ctypes.byref(cfg))
        self._check(r, "failed to get configuration")
        return cfg

    def get_configuration_default(self):
        """Return a safe default ``jsScanHeadConfiguration`` for this scan head."""
        cfg = _n.jsScanHeadConfiguration()
        r = _n.lib.jsScanHeadGetConfigurationDefault(
            self._handle, ctypes.byref(cfg)
        )
        self._check(r, "failed to get default configuration")
        return cfg

    def set_cable_orientation(self, orientation):
        """Set the :class:`~joescan_pinchot.enums.CableOrientation`."""
        r = _n.lib.jsScanHeadSetCableOrientation(self._handle, int(orientation))
        self._check(r, "failed to set cable orientation")

    def get_cable_orientation(self):
        """Return the current :class:`~joescan_pinchot.enums.CableOrientation`."""
        v = ctypes.c_int32()
        r = _n.lib.jsScanHeadGetCableOrientation(self._handle, ctypes.byref(v))
        self._check(r, "failed to get cable orientation")
        return CableOrientation(v.value)

    # -- alignment --------------------------------------------------------

    def set_alignment(self, roll_degrees, shift_x, shift_y):
        """Set alignment (roll + shift) for both cameras/lasers."""
        r = _n.lib.jsScanHeadSetAlignment(
            self._handle, float(roll_degrees), float(shift_x), float(shift_y)
        )
        self._check(r, "failed to set alignment")

    def set_alignment_camera(self, camera, roll_degrees, shift_x, shift_y):
        """Set alignment for a single camera."""
        r = _n.lib.jsScanHeadSetAlignmentCamera(
            self._handle, int(camera), float(roll_degrees), float(shift_x),
            float(shift_y)
        )
        self._check(r, "failed to set camera alignment")

    def get_alignment_camera(self, camera):
        """Return ``(roll_degrees, shift_x, shift_y)`` for a camera."""
        roll = ctypes.c_double()
        sx = ctypes.c_double()
        sy = ctypes.c_double()
        r = _n.lib.jsScanHeadGetAlignmentCamera(
            self._handle, int(camera), ctypes.byref(roll), ctypes.byref(sx),
            ctypes.byref(sy)
        )
        self._check(r, "failed to get camera alignment")
        return (roll.value, sx.value, sy.value)

    def set_alignment_laser(self, laser, roll_degrees, shift_x, shift_y):
        """Set alignment for a single laser."""
        r = _n.lib.jsScanHeadSetAlignmentLaser(
            self._handle, int(laser), float(roll_degrees), float(shift_x),
            float(shift_y)
        )
        self._check(r, "failed to set laser alignment")

    def get_alignment_laser(self, laser):
        """Return ``(roll_degrees, shift_x, shift_y)`` for a laser."""
        roll = ctypes.c_double()
        sx = ctypes.c_double()
        sy = ctypes.c_double()
        r = _n.lib.jsScanHeadGetAlignmentLaser(
            self._handle, int(laser), ctypes.byref(roll), ctypes.byref(sx),
            ctypes.byref(sy)
        )
        self._check(r, "failed to get laser alignment")
        return (roll.value, sx.value, sy.value)

    # -- exclusion mask ---------------------------------------------------

    def set_exclusion_mask_camera(self, camera, mask):
        """Apply a :class:`~joescan_pinchot._native.jsExclusionMask` to a camera."""
        r = _n.lib.jsScanHeadSetExclusionMaskCamera(
            self._handle, int(camera), ctypes.byref(mask)
        )
        self._check(r, "failed to set camera exclusion mask")

    def set_exclusion_mask_laser(self, laser, mask):
        """Apply a :class:`~joescan_pinchot._native.jsExclusionMask` to a laser."""
        r = _n.lib.jsScanHeadSetExclusionMaskLaser(
            self._handle, int(laser), ctypes.byref(mask)
        )
        self._check(r, "failed to set laser exclusion mask")

    def get_exclusion_mask_camera(self, camera):
        """Return the exclusion mask currently applied to a camera."""
        mask = _n.jsExclusionMask()
        r = _n.lib.jsScanHeadGetExclusionMaskCamera(
            self._handle, int(camera), ctypes.byref(mask)
        )
        self._check(r, "failed to get camera exclusion mask")
        return mask

    def get_exclusion_mask_laser(self, laser):
        """Return the exclusion mask currently applied to a laser."""
        mask = _n.jsExclusionMask()
        r = _n.lib.jsScanHeadGetExclusionMaskLaser(
            self._handle, int(laser), ctypes.byref(mask)
        )
        self._check(r, "failed to get laser exclusion mask")
        return mask

    # -- brightness correction (BETA) ------------------------------------

    def set_brightness_correction_camera(self, camera, correction):
        """Apply a beta brightness correction to a camera."""
        r = _n.lib.jsScanHeadSetBrightnessCorrectionCamera_BETA(
            self._handle, int(camera), ctypes.byref(correction)
        )
        self._check(r, "failed to set camera brightness correction")

    def set_brightness_correction_laser(self, laser, correction):
        """Apply a beta brightness correction to a laser."""
        r = _n.lib.jsScanHeadSetBrightnessCorrectionLaser_BETA(
            self._handle, int(laser), ctypes.byref(correction)
        )
        self._check(r, "failed to set laser brightness correction")

    def get_brightness_correction_camera(self, camera):
        """Return the beta brightness correction applied to a camera."""
        c = _n.jsBrightnessCorrection_BETA()
        r = _n.lib.jsScanHeadGetBrightnessCorrectionCamera_BETA(
            self._handle, int(camera), ctypes.byref(c)
        )
        self._check(r, "failed to get camera brightness correction")
        return c

    def get_brightness_correction_laser(self, laser):
        """Return the beta brightness correction applied to a laser."""
        c = _n.jsBrightnessCorrection_BETA()
        r = _n.lib.jsScanHeadGetBrightnessCorrectionLaser_BETA(
            self._handle, int(laser), ctypes.byref(c)
        )
        self._check(r, "failed to get laser brightness correction")
        return c

    # -- windows ----------------------------------------------------------

    def set_window_unconstrained(self, camera=None, laser=None):
        """Reset the scan window to full field of view.

        With no argument this resets the whole scan head; pass ``camera`` or
        ``laser`` to reset only that element.
        """
        if camera is not None:
            r = _n.lib.jsScanHeadSetWindowUnconstrainedCamera(
                self._handle, int(camera))
        elif laser is not None:
            r = _n.lib.jsScanHeadSetWindowUnconstrainedLaser(
                self._handle, int(laser))
        else:
            r = _n.lib.jsScanHeadSetWindowUnconstrained(self._handle)
        self._check(r, "failed to set unconstrained window")

    def set_window_rectangular(self, top, bottom, left, right,
                               camera=None, laser=None):
        """Set a rectangular scan window (scan-system units).

        Pass ``camera`` or ``laser`` to restrict the window to that element.
        """
        if camera is not None:
            r = _n.lib.jsScanHeadSetWindowRectangularCamera(
                self._handle, int(camera), float(top), float(bottom),
                float(left), float(right))
        elif laser is not None:
            r = _n.lib.jsScanHeadSetWindowRectangularLaser(
                self._handle, int(laser), float(top), float(bottom),
                float(left), float(right))
        else:
            r = _n.lib.jsScanHeadSetWindowRectangular(
                self._handle, float(top), float(bottom), float(left),
                float(right))
        self._check(r, "failed to set rectangular window")

    def set_polygon_window(self, points, camera=None, laser=None):
        """Set a convex polygonal scan window.

        :param points: Iterable of ``(x, y)`` pairs ordered clockwise.  The
            first and last points are connected automatically.
        """
        points = list(points)
        n = len(points)
        arr = (_n.jsCoordinate * n)()
        for i, (x, y) in enumerate(points):
            arr[i].x = float(x)
            arr[i].y = float(y)
        if camera is not None:
            r = _n.lib.jsScanHeadSetPolygonWindowCamera(
                self._handle, int(camera), arr, n)
        elif laser is not None:
            r = _n.lib.jsScanHeadSetPolygonWindowLaser(
                self._handle, int(laser), arr, n)
        else:
            r = _n.lib.jsScanHeadSetPolygonWindow(self._handle, arr, n)
        self._check(r, "failed to set polygon window")

    def get_window_type(self, camera=None, laser=None):
        """Return the :class:`~joescan_pinchot.enums.ScanWindowType` of an element."""
        v = ctypes.c_int32()
        if camera is not None:
            r = _n.lib.jsScanHeadGetWindowTypeCamera(
                self._handle, int(camera), ctypes.byref(v))
        elif laser is not None:
            r = _n.lib.jsScanHeadGetWindowTypeLaser(
                self._handle, int(laser), ctypes.byref(v))
        else:
            raise ValueError("specify either camera or laser")
        self._check(r, "failed to get window type")
        return ScanWindowType(v.value)

    def get_window(self, camera=None, laser=None):
        """Return the window as a list of ``(x, y)`` points for an element."""
        if camera is not None:
            n = _n.lib.jsScanHeadGetNumberWindowPointsCamera(
                self._handle, int(camera))
        elif laser is not None:
            n = _n.lib.jsScanHeadGetNumberWindowPointsLaser(
                self._handle, int(laser))
        else:
            raise ValueError("specify either camera or laser")
        self._check(n, "failed to get number of window points")
        if n <= 0:
            return []
        arr = (_n.jsCoordinate * n)()
        if camera is not None:
            r = _n.lib.jsScanHeadGetWindowCamera(
                self._handle, int(camera), arr)
        else:
            r = _n.lib.jsScanHeadGetWindowLaser(
                self._handle, int(laser), arr)
        self._check(r, "failed to get window")
        return [(arr[i].x, arr[i].y) for i in range(n)]

    # -- status / profiles ------------------------------------------------

    def get_status(self):
        """Return the last reported ``jsScanHeadStatus`` struct."""
        status = _n.jsScanHeadStatus()
        r = _n.lib.jsScanHeadGetStatus(self._handle, ctypes.byref(status))
        self._check(r, "failed to get status")
        return status

    def get_profiles_available(self):
        """Number of profiles currently buffered and ready to read."""
        r = _n.lib.jsScanHeadGetProfilesAvailable(self._handle)
        return self._check(r, "failed to get profiles available")

    def wait_until_profiles_available(self, count, timeout_us):
        """Block until ``count`` profiles are ready or ``timeout_us`` elapses."""
        r = _n.lib.jsScanHeadWaitUntilProfilesAvailable(
            self._handle, int(count), int(timeout_us)
        )
        return self._check(r, "failed waiting for profiles")

    def clear_profiles(self):
        """Empty the client-side profile buffer for this scan head."""
        r = _n.lib.jsScanHeadClearProfiles(self._handle)
        self._check(r, "failed to clear profiles")

    def get_profiles(self, max_profiles=JS_SCAN_HEAD_PROFILES_MAX):
        """Read up to ``max_profiles`` and return a list of :class:`Profile`."""
        max_profiles = min(int(max_profiles), JS_SCAN_HEAD_PROFILES_MAX)
        arr = (_n.jsProfile * max_profiles)()
        r = _n.lib.jsScanHeadGetProfiles(self._handle, arr, max_profiles)
        self._check(r, "failed to get profiles")
        return [Profile(arr[i]) for i in range(r)]

    def get_raw_profiles(self, max_profiles=JS_SCAN_HEAD_PROFILES_MAX):
        """Read up to ``max_profiles`` raw ``jsRawProfile`` structs (as a list)."""
        max_profiles = min(int(max_profiles), JS_SCAN_HEAD_PROFILES_MAX)
        arr = (_n.jsRawProfile * max_profiles)()
        r = _n.lib.jsScanHeadGetRawProfiles(self._handle, arr, max_profiles)
        self._check(r, "failed to get raw profiles")
        return [arr[i] for i in range(r)]

    # -- diagnostics ------------------------------------------------------

    def get_diagnostic_profile_camera(self, camera, laser_on_time_us,
                                      camera_exposure_time_us,
                                      mode=DiagnosticMode.FIXED_EXPOSURE):
        """Capture a single diagnostic raw profile using ``camera``."""
        profile = _n.jsRawProfile()
        r = _n.lib.jsScanHeadGetDiagnosticProfileCamera(
            self._handle, int(camera), int(mode), int(laser_on_time_us),
            int(camera_exposure_time_us), ctypes.byref(profile)
        )
        self._check(r, "failed to get diagnostic profile")
        return profile

    def get_diagnostic_profile_laser(self, laser, laser_on_time_us,
                                     camera_exposure_time_us,
                                     mode=DiagnosticMode.FIXED_EXPOSURE):
        """Capture a single diagnostic raw profile using ``laser``."""
        profile = _n.jsRawProfile()
        r = _n.lib.jsScanHeadGetDiagnosticProfileLaser(
            self._handle, int(laser), int(mode), int(laser_on_time_us),
            int(camera_exposure_time_us), ctypes.byref(profile)
        )
        self._check(r, "failed to get diagnostic profile")
        return profile

    def __repr__(self):
        return f"ScanHead(id={self.id}, serial={self.serial})"
