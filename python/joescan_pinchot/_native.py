"""Low-level ``ctypes`` binding to the Pinchot shared library.

This module mirrors ``joescan_pinchot.h`` one-to-one: every ``struct`` becomes a
:class:`ctypes.Structure` (packed to match the C ``#pragma pack(push, 1)``) and
every exported ``js*`` function is given an explicit ``argtypes``/``restype``.

Most users should prefer the high-level :mod:`joescan_pinchot.api` classes.  The
raw library handle is exposed here as :data:`lib` for advanced use.
"""

import ctypes
import os
import sys
from ctypes import (
    POINTER,
    c_bool,
    c_char,
    c_char_p,
    c_double,
    c_float,
    c_int,
    c_int32,
    c_int64,
    c_uint8,
    c_uint32,
    c_uint64,
)

from .enums import (
    JS_CAMERA_IMAGE_DATA_LEN,
    JS_CAMERA_IMAGE_DATA_MAX_HEIGHT,
    JS_CAMERA_IMAGE_DATA_MAX_WIDTH,
    JS_CLIENT_NAME_STR_MAX_LEN,
    JS_PROFILE_DATA_LEN,
    JS_RAW_PROFILE_DATA_LEN,
    JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN,
    JS_SCAN_HEAD_TYPE_STR_MAX_LEN,
)

# jsEncoder JS_ENCODER_MAX == 3; encoder_values arrays are sized to this.
_ENCODER_MAX = 3

# Opaque handle typedefs (``typedef int64_t``).
jsScanSystem = c_int64
jsScanHead = c_int64

# Enums in the header carry a FORCE_INT32_SIZE member, i.e. they are 32-bit.
_enum_t = c_int32


# --------------------------------------------------------------------------
# Library loading
# --------------------------------------------------------------------------

def _candidate_names():
    if sys.platform.startswith("win"):
        return ["pinchot.dll"]
    if sys.platform == "darwin":
        return ["libpinchot.dylib", "pinchot.dylib"]
    return ["libpinchot.so", "pinchot.so"]


def _candidate_dirs():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.join(here, "..", "..")
    dirs = [
        os.environ.get("PINCHOT_LIB_DIR"),
        here,
        os.path.join(here, "lib"),
        # Typical CMake build output locations relative to the repo. Windows
        # multi-config generators nest under Release/Debug; single-config
        # generators (Ninja/Make, common on Linux) emit into the build dir.
        os.path.join(repo, "build", "Release"),
        os.path.join(repo, "build", "Debug"),
        os.path.join(repo, "build"),
        os.path.join(repo, "build-ninja"),
    ]
    return [d for d in dirs if d]


def load_library(path=None):
    """Locate and load the native Pinchot shared library.

    :param path: Explicit path to the shared library.  When ``None`` the
        ``PINCHOT_LIB_DIR`` environment variable, the package directory, and the
        repository ``build`` directories are searched, followed by the system
        loader search path.
    :returns: The loaded :class:`ctypes.CDLL` handle.
    :raises OSError: If the library cannot be found or loaded.
    """
    errors = []

    explicit = [path] if path else []
    for candidate in explicit:
        try:
            return ctypes.CDLL(candidate)
        except OSError as exc:  # pragma: no cover - platform dependent
            errors.append(f"{candidate}: {exc}")

    if not explicit:
        for directory in _candidate_dirs():
            for name in _candidate_names():
                full = os.path.join(directory, name)
                if os.path.isfile(full):
                    try:
                        return ctypes.CDLL(full)
                    except OSError as exc:  # pragma: no cover
                        errors.append(f"{full}: {exc}")
        # Fall back to the system loader search path.
        for name in _candidate_names():
            try:
                return ctypes.CDLL(name)
            except OSError as exc:  # pragma: no cover
                errors.append(f"{name}: {exc}")

    detail = "\n  ".join(errors) if errors else "no candidates tried"
    raise OSError(
        "Could not load the Pinchot native library. Set PINCHOT_LIB_DIR to the "
        "directory containing pinchot.dll / libpinchot.so, or pass an explicit "
        "path to load_library().\nTried:\n  " + detail
    )


# --------------------------------------------------------------------------
# Structures (packed to match `#pragma pack(push, 1)` in the header)
# --------------------------------------------------------------------------

class jsDiscovered(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("serial_number", c_uint32),
        ("type", _enum_t),
        ("type_str", c_char * JS_SCAN_HEAD_TYPE_STR_MAX_LEN),
        ("firmware_version_major", c_uint32),
        ("firmware_version_minor", c_uint32),
        ("firmware_version_patch", c_uint32),
        ("ip_addr", c_uint32),
        ("client_name_str", c_char * JS_CLIENT_NAME_STR_MAX_LEN),
        ("client_ip_addr", c_uint32),
        ("client_netmask", c_uint32),
        ("link_speed_mbps", c_uint32),
        ("state", _enum_t),
    ]


class jsScanSyncDiscovered(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("serial_number", c_uint32),
        ("firmware_version_major", c_uint32),
        ("firmware_version_minor", c_uint32),
        ("firmware_version_patch", c_uint32),
        ("ip_addr", c_uint32),
    ]


class jsScanHeadCapabilities(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("camera_brightness_bit_depth", c_uint32),
        ("max_camera_image_height", c_uint32),
        ("max_camera_image_width", c_uint32),
        ("min_scan_period_us", c_uint32),
        ("max_scan_period_us", c_uint32),
        ("num_cameras", c_uint32),
        ("num_encoders", c_uint32),
        ("num_lasers", c_uint32),
    ]


class jsScanHeadConfiguration(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("camera_exposure_time_min_us", c_uint32),
        ("camera_exposure_time_max_us", c_uint32),
        ("camera_exposure_time_def_us", c_uint32),
        ("laser_on_time_min_us", c_uint32),
        ("laser_on_time_max_us", c_uint32),
        ("laser_on_time_def_us", c_uint32),
        ("laser_detection_threshold", c_uint32),
        ("saturation_threshold", c_uint32),
        ("saturation_percentage", c_uint32),
    ]


class jsExclusionMask(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        (
            "bitmap",
            (c_uint8 * JS_CAMERA_IMAGE_DATA_MAX_WIDTH)
            * JS_CAMERA_IMAGE_DATA_MAX_HEIGHT,
        ),
    ]


class jsBrightnessCorrection_BETA(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("offset", c_uint8),
        ("scale_factors", c_float * JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN),
    ]


class jsScanHeadStatus(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("global_time_ns", c_uint64),
        ("encoder_values", c_int64 * _ENCODER_MAX),
        ("num_encoder_values", c_uint32),
        ("camera_a_pixels_in_window", c_int32),
        ("camera_b_pixels_in_window", c_int32),
        ("camera_a_temp", c_int32),
        ("camera_b_temp", c_int32),
        ("num_profiles_sent", c_uint32),
        ("state", _enum_t),
        ("is_laser_disable", c_bool),
    ]


class jsScanSyncStatus(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("encoder", c_int64),
        ("timestamp_ns", c_uint64),
        ("sync_timestamp_ns", c_uint64),
        ("aux_y_timestamp_ns", c_uint64),
        ("index_z_timestamp_ns", c_uint64),
        ("laser_disable_timestamp_ns", c_uint64),
        ("serial", c_uint32),
        ("is_fault_a", c_bool),
        ("is_fault_b", c_bool),
        ("is_sync", c_bool),
        ("is_aux_y", c_bool),
        ("is_index_z", c_bool),
        ("is_laser_disable", c_bool),
    ]


class jsCoordinate(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("x", c_double),
        ("y", c_double),
    ]


class jsProfileData(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("x", c_int32),
        ("y", c_int32),
        ("brightness", c_int32),
    ]


class jsProfile(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("scan_head_id", c_uint32),
        ("camera", _enum_t),
        ("laser", _enum_t),
        ("timestamp_ns", c_uint64),
        ("flags", c_uint32),
        ("sequence_number", c_uint32),
        ("encoder_values", c_int64 * _ENCODER_MAX),
        ("num_encoder_values", c_uint32),
        ("laser_on_time_us", c_uint32),
        ("format", _enum_t),
        ("packets_received", c_uint32),
        ("packets_expected", c_uint32),
        ("data_len", c_uint32),
        ("reserved_0", c_uint64),
        ("reserved_1", c_uint64),
        ("reserved_2", c_uint64),
        ("reserved_3", c_uint64),
        ("reserved_4", c_uint64),
        ("reserved_5", c_uint64),
        ("data", jsProfileData * JS_PROFILE_DATA_LEN),
    ]


class jsRawProfile(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("scan_head_id", c_uint32),
        ("camera", _enum_t),
        ("laser", _enum_t),
        ("timestamp_ns", c_uint64),
        ("flags", c_uint32),
        ("sequence_number", c_uint32),
        ("encoder_values", c_int64 * _ENCODER_MAX),
        ("num_encoder_values", c_uint32),
        ("laser_on_time_us", c_uint32),
        ("format", _enum_t),
        ("packets_received", c_uint32),
        ("packets_expected", c_uint32),
        ("data_len", c_uint32),
        ("data_valid_brightness", c_uint32),
        ("data_valid_xy", c_uint32),
        ("reserved_0", c_uint64),
        ("reserved_1", c_uint64),
        ("reserved_2", c_uint64),
        ("reserved_3", c_uint64),
        ("reserved_4", c_uint64),
        ("reserved_5", c_uint64),
        ("data", jsProfileData * JS_RAW_PROFILE_DATA_LEN),
    ]


class jsCameraImage(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("scan_head_id", c_uint32),
        ("camera", _enum_t),
        ("laser", _enum_t),
        ("timestamp_ns", c_uint64),
        ("encoder_values", c_uint64 * _ENCODER_MAX),
        ("num_encoder_values", c_uint32),
        ("camera_exposure_time_us", c_uint32),
        ("laser_on_time_us", c_uint32),
        ("image_height", c_uint32),
        ("image_width", c_uint32),
        ("data", c_uint8 * JS_CAMERA_IMAGE_DATA_LEN),
    ]


# --------------------------------------------------------------------------
# Function prototype table
# --------------------------------------------------------------------------

# Each entry: (name, restype, [argtypes])
_PROTOTYPES = [
    # --- Miscellaneous / version ---
    ("jsGetAPIVersion", None, [POINTER(c_char_p)]),
    ("jsGetAPISemanticVersion", None,
     [POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint32)]),
    ("jsGetError", None, [c_int32, POINTER(c_char_p)]),
    ("jsScanSystemGetLastErrorExtended", c_int32,
     [jsScanSystem, POINTER(c_char_p)]),
    ("jsScanHeadGetLastErrorExtended", c_int32,
     [jsScanHead, POINTER(c_char_p)]),
    ("jsProfileInit", None, [POINTER(jsProfile)]),
    ("jsRawProfileInit", None, [POINTER(jsRawProfile)]),
    ("jsPowerCycleScanHead", c_int32, [c_uint32]),

    # --- Scan system lifecycle / discovery ---
    ("jsScanSystemCreate", jsScanSystem, [_enum_t]),
    ("jsScanSystemFree", None, [jsScanSystem]),
    ("jsScanSystemDiscover", c_int, [jsScanSystem]),
    ("jsScanSystemGetDiscovered", c_int,
     [jsScanSystem, POINTER(jsDiscovered), c_uint32]),
    ("jsScanSystemScanSyncDiscover", c_int, [jsScanSystem]),
    ("jsScanSystemGetScanSyncDiscovered", c_int,
     [jsScanSystem, POINTER(jsScanSyncDiscovered), c_uint32]),

    # --- ScanSync encoder configuration / status ---
    ("jsScanSystemSetScanSyncEncoder", c_int,
     [jsScanSystem, c_uint32, c_uint32, c_uint32]),
    ("jsScanSystemGetScanSyncEncoder", c_int,
     [jsScanSystem, POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint32)]),
    ("jsScanSystemSetDefaultScanSyncEncoder", c_int, [jsScanSystem]),
    ("jsScanSystemGetScanSyncStatus", c_int,
     [jsScanSystem, c_uint32, POINTER(jsScanSyncStatus)]),
    ("jsScanSystemGetEncoder", c_int,
     [jsScanSystem, _enum_t, POINTER(c_int64)]),

    # --- Scan head creation / lookup ---
    ("jsScanSystemCreateScanHead", jsScanHead,
     [jsScanSystem, c_uint32, c_uint32]),
    ("jsScanSystemGetScanHeadById", jsScanHead, [jsScanSystem, c_uint32]),
    ("jsScanSystemGetScanHeadBySerial", jsScanHead, [jsScanSystem, c_uint32]),
    ("jsScanSystemGetNumberScanHeads", c_int32, [jsScanSystem]),

    # --- Connection ---
    ("jsScanSystemConnect", c_int32, [jsScanSystem, c_int32]),
    ("jsScanSystemDisconnect", c_int32, [jsScanSystem]),
    ("jsScanSystemIsConnected", c_bool, [jsScanSystem]),

    # --- Phase table ---
    ("jsScanSystemPhaseClearAll", c_int32, [jsScanSystem]),
    ("jsScanSystemPhaseCreate", c_int32, [jsScanSystem]),
    ("jsScanSystemPhaseInsertCamera", c_int32,
     [jsScanSystem, jsScanHead, _enum_t]),
    ("jsScanSystemPhaseInsertLaser", c_int32,
     [jsScanSystem, jsScanHead, _enum_t]),
    ("jsScanSystemPhaseInsertConfigurationCamera", c_int32,
     [jsScanSystem, jsScanHead, POINTER(jsScanHeadConfiguration), _enum_t]),
    ("jsScanSystemPhaseInsertConfigurationLaser", c_int32,
     [jsScanSystem, jsScanHead, POINTER(jsScanHeadConfiguration), _enum_t]),

    # --- Configure / scan period ---
    ("jsScanSystemGetMinScanPeriod", c_int32, [jsScanSystem]),
    ("jsScanSystemConfigure", c_int32, [jsScanSystem]),
    ("jsScanSystemIsConfigured", c_bool, [jsScanSystem]),

    # --- Profile scanning ---
    ("jsScanSystemStartScanning", c_int32,
     [jsScanSystem, c_uint32, _enum_t]),
    ("jsScanSystemStopScanning", c_int32, [jsScanSystem]),
    ("jsScanSystemIsScanning", c_bool, [jsScanSystem]),

    # --- Frame scanning ---
    ("jsScanSystemStartFrameScanning", c_int32,
     [jsScanSystem, c_uint32, _enum_t]),
    ("jsScanSystemGetProfilesPerFrame", c_int32, [jsScanSystem]),
    ("jsScanSystemWaitUntilFrameAvailable", c_int32,
     [jsScanSystem, c_uint32]),
    ("jsScanSystemIsFrameAvailable", c_bool, [jsScanSystem]),
    ("jsScanSystemClearFrames", c_int32, [jsScanSystem]),
    ("jsScanSystemGetFrame", c_int32, [jsScanSystem, POINTER(jsProfile)]),
    ("jsScanSystemGetRawFrame", c_int32,
     [jsScanSystem, POINTER(jsRawProfile)]),

    # --- Idle scanning ---
    ("jsScanSystemSetIdleScanPeriod", c_int32, [jsScanSystem, c_uint32]),
    ("jsScanSystemGetIdleScanPeriod", c_int32,
     [jsScanSystem, POINTER(c_uint32)]),
    ("jsScanSystemDisableIdleScanning", c_int32, [jsScanSystem]),
    ("jsScanSystemIsIdleScanningEnabled", c_bool, [jsScanSystem]),

    # --- Scan head info ---
    ("jsScanHeadGetType", _enum_t, [jsScanHead]),
    ("jsScanHeadGetId", c_uint32, [jsScanHead]),
    ("jsScanHeadGetSerial", c_uint32, [jsScanHead]),
    ("jsScanHeadGetCapabilities", c_int32,
     [jsScanHead, POINTER(jsScanHeadCapabilities)]),
    ("jsScanHeadGetFirmwareVersion", c_int32,
     [jsScanHead, POINTER(c_uint32), POINTER(c_uint32), POINTER(c_uint32)]),
    ("jsScanHeadIsConnected", c_bool, [jsScanHead]),

    # --- Scan head configuration ---
    ("jsScanHeadSetConfiguration", c_int32,
     [jsScanHead, POINTER(jsScanHeadConfiguration)]),
    ("jsScanHeadGetConfiguration", c_int32,
     [jsScanHead, POINTER(jsScanHeadConfiguration)]),
    ("jsScanHeadGetConfigurationDefault", c_int32,
     [jsScanHead, POINTER(jsScanHeadConfiguration)]),
    ("jsScanHeadSetCableOrientation", c_int32, [jsScanHead, _enum_t]),
    ("jsScanHeadGetCableOrientation", c_int32,
     [jsScanHead, POINTER(_enum_t)]),

    # --- Alignment ---
    ("jsScanHeadSetAlignment", c_int32,
     [jsScanHead, c_double, c_double, c_double]),
    ("jsScanHeadSetAlignmentCamera", c_int32,
     [jsScanHead, _enum_t, c_double, c_double, c_double]),
    ("jsScanHeadGetAlignmentCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(c_double), POINTER(c_double),
      POINTER(c_double)]),
    ("jsScanHeadSetAlignmentLaser", c_int32,
     [jsScanHead, _enum_t, c_double, c_double, c_double]),
    ("jsScanHeadGetAlignmentLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(c_double), POINTER(c_double),
      POINTER(c_double)]),

    # --- Exclusion mask ---
    ("jsScanHeadSetExclusionMaskCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(jsExclusionMask)]),
    ("jsScanHeadSetExclusionMaskLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(jsExclusionMask)]),
    ("jsScanHeadGetExclusionMaskCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(jsExclusionMask)]),
    ("jsScanHeadGetExclusionMaskLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(jsExclusionMask)]),

    # --- Brightness correction (BETA) ---
    ("jsScanHeadSetBrightnessCorrectionCamera_BETA", c_int32,
     [jsScanHead, _enum_t, POINTER(jsBrightnessCorrection_BETA)]),
    ("jsScanHeadSetBrightnessCorrectionLaser_BETA", c_int32,
     [jsScanHead, _enum_t, POINTER(jsBrightnessCorrection_BETA)]),
    ("jsScanHeadGetBrightnessCorrectionCamera_BETA", c_int32,
     [jsScanHead, _enum_t, POINTER(jsBrightnessCorrection_BETA)]),
    ("jsScanHeadGetBrightnessCorrectionLaser_BETA", c_int32,
     [jsScanHead, _enum_t, POINTER(jsBrightnessCorrection_BETA)]),

    # --- Windows ---
    ("jsScanHeadSetWindowUnconstrained", c_int32, [jsScanHead]),
    ("jsScanHeadSetWindowUnconstrainedCamera", c_int32,
     [jsScanHead, _enum_t]),
    ("jsScanHeadSetWindowUnconstrainedLaser", c_int32,
     [jsScanHead, _enum_t]),
    ("jsScanHeadSetWindowRectangular", c_int32,
     [jsScanHead, c_double, c_double, c_double, c_double]),
    ("jsScanHeadSetWindowRectangularCamera", c_int32,
     [jsScanHead, _enum_t, c_double, c_double, c_double, c_double]),
    ("jsScanHeadSetWindowRectangularLaser", c_int32,
     [jsScanHead, _enum_t, c_double, c_double, c_double, c_double]),
    ("jsScanHeadSetPolygonWindow", c_int32,
     [jsScanHead, POINTER(jsCoordinate), c_uint32]),
    ("jsScanHeadSetPolygonWindowCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(jsCoordinate), c_uint32]),
    ("jsScanHeadSetPolygonWindowLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(jsCoordinate), c_uint32]),
    ("jsScanHeadGetWindowTypeCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(_enum_t)]),
    ("jsScanHeadGetWindowTypeLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(_enum_t)]),
    ("jsScanHeadGetNumberWindowPointsCamera", c_int32,
     [jsScanHead, _enum_t]),
    ("jsScanHeadGetNumberWindowPointsLaser", c_int32,
     [jsScanHead, _enum_t]),
    ("jsScanHeadGetWindowCamera", c_int32,
     [jsScanHead, _enum_t, POINTER(jsCoordinate)]),
    ("jsScanHeadGetWindowLaser", c_int32,
     [jsScanHead, _enum_t, POINTER(jsCoordinate)]),

    # --- Status / profiles ---
    ("jsScanHeadGetStatus", c_int32,
     [jsScanHead, POINTER(jsScanHeadStatus)]),
    ("jsScanHeadGetProfilesAvailable", c_int32, [jsScanHead]),
    ("jsScanHeadWaitUntilProfilesAvailable", c_int32,
     [jsScanHead, c_uint32, c_uint32]),
    ("jsScanHeadClearProfiles", c_int32, [jsScanHead]),
    ("jsScanHeadGetProfiles", c_int32,
     [jsScanHead, POINTER(jsProfile), c_uint32]),
    ("jsScanHeadGetRawProfiles", c_int32,
     [jsScanHead, POINTER(jsRawProfile), c_uint32]),

    # --- Diagnostics ---
    ("jsScanHeadGetDiagnosticProfileCamera", c_int32,
     [jsScanHead, _enum_t, _enum_t, c_uint32, c_uint32,
      POINTER(jsRawProfile)]),
    ("jsScanHeadGetDiagnosticProfileLaser", c_int32,
     [jsScanHead, _enum_t, _enum_t, c_uint32, c_uint32,
      POINTER(jsRawProfile)]),
    ("jsScanHeadGetDiagnosticImageCamera", c_int32,
     [jsScanHead, _enum_t, _enum_t, c_uint32, c_uint32,
      POINTER(jsCameraImage)]),
    ("jsScanHeadGetDiagnosticImageLaser", c_int32,
     [jsScanHead, _enum_t, _enum_t, c_uint32, c_uint32,
      POINTER(jsCameraImage)]),
    ("jsScanHeadGetDiagnosticImage", c_int32,
     [jsScanHead, _enum_t, _enum_t, _enum_t, c_uint32, c_uint32,
      POINTER(jsCameraImage)]),
]


class _MissingSymbol:
    """Placeholder installed for functions absent from an older library build.

    Calling it raises a clear error rather than the opaque ``AttributeError``
    ctypes would otherwise produce on symbol lookup.
    """

    def __init__(self, name):
        self._name = name

    def __call__(self, *args, **kwargs):
        raise NotImplementedError(
            f"'{self._name}' is not available in the loaded Pinchot library. "
            "The native library is likely older than this wrapper; rebuild or "
            "update it to a version that exports this symbol."
        )


def _bind(cdll):
    """Apply ``argtypes``/``restype`` to every prototype present in ``cdll``.

    Symbols missing from an older library build get a :class:`_MissingSymbol`
    stub so that only the specific call fails, with a helpful message.
    """
    for name, restype, argtypes in _PROTOTYPES:
        try:
            fn = getattr(cdll, name)
        except AttributeError:
            setattr(cdll, name, _MissingSymbol(name))
            continue
        fn.restype = restype
        fn.argtypes = argtypes


#: The loaded native library handle, bound with prototypes.  Lazily populated by
#: :func:`init` (called on package import).
lib = None


def init(path=None):
    """Load and bind the native library, returning the handle."""
    global lib
    lib = load_library(path)
    _bind(lib)
    return lib
