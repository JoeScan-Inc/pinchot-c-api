"""Python wrapper for the JoeScan Pinchot C API.

This package binds the native ``pinchot`` shared library (``pinchot.dll`` on
Windows, ``libpinchot.so`` on Linux) via :mod:`ctypes` and layers a Pythonic
object-oriented interface on top.

Quick start::

    import joescan_pinchot as js

    with js.ScanSystem(js.Units.INCHES) as system:
        for head in system.get_discovered():
            print(head)

If the native library is not on the default search path, point the wrapper at
it before using the API::

    import joescan_pinchot as js
    js.initialize(r"C:\\path\\to\\pinchot.dll")

or set the ``PINCHOT_LIB_DIR`` environment variable to the directory that
contains it.
"""

from . import _native
from .enums import (  # noqa: F401
    JS_CAMERA_IMAGE_DATA_LEN,
    JS_CAMERA_IMAGE_DATA_MAX_HEIGHT,
    JS_CAMERA_IMAGE_DATA_MAX_WIDTH,
    JS_CLIENT_NAME_STR_MAX_LEN,
    JS_PROFILE_DATA_INVALID_BRIGHTNESS,
    JS_PROFILE_DATA_INVALID_XY,
    JS_PROFILE_DATA_LEN,
    JS_RAW_PROFILE_DATA_LEN,
    JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN,
    JS_SCAN_HEAD_INVALID_SERIAL,
    JS_SCAN_HEAD_PROFILES_MAX,
    JS_SCAN_HEAD_TYPE_STR_MAX_LEN,
    JS_SCANSYNC_INVALID_ENCODER,
    JS_SCANSYNC_INVALID_SERIAL,
    CableOrientation,
    Camera,
    DataFormat,
    DiagnosticMode,
    Encoder,
    Error,
    Laser,
    ProfileFlags,
    ScanHeadState,
    ScanHeadType,
    ScanWindowType,
    Units,
)
from .exceptions import PinchotError

__version__ = "16.3.2"

# Native structures re-exported for advanced users who work with raw structs.
from ._native import (  # noqa: E402,F401
    jsBrightnessCorrection_BETA,
    jsCameraImage,
    jsCoordinate,
    jsExclusionMask,
    jsProfileData,
    jsRawProfile,
    jsScanHeadCapabilities,
    jsScanHeadConfiguration,
    jsScanHeadStatus,
    jsScanSyncStatus,
)

#: Populated when the native library loads successfully; otherwise holds the
#: :class:`OSError` describing why loading failed.
load_error = None

try:
    _native.init()
except OSError as exc:  # pragma: no cover - environment dependent
    load_error = exc

# The high-level API references ``_native.lib`` and therefore must be imported
# after the load attempt above.
from .api import (  # noqa: E402,F401
    CameraImage,
    Discovered,
    Profile,
    ProfilePoint,
    ScanHead,
    ScanSyncDiscovered,
    ScanSystem,
    error_to_string,
    get_api_semantic_version,
    get_api_version,
    power_cycle_scan_head,
)


def initialize(path=None):
    """Load (or reload) the native library from ``path`` and clear any error.

    :param path: Explicit path to the shared library, or ``None`` to search the
        default locations again.
    :returns: The loaded :class:`ctypes.CDLL` handle.
    """
    global load_error
    handle = _native.init(path)
    load_error = None
    return handle


__all__ = [
    "ScanSystem",
    "ScanHead",
    "Profile",
    "ProfilePoint",
    "CameraImage",
    "Discovered",
    "ScanSyncDiscovered",
    "PinchotError",
    "Units",
    "CableOrientation",
    "ScanWindowType",
    "ScanHeadType",
    "Camera",
    "Laser",
    "Encoder",
    "ProfileFlags",
    "DataFormat",
    "DiagnosticMode",
    "ScanHeadState",
    "Error",
    "get_api_version",
    "get_api_semantic_version",
    "error_to_string",
    "power_cycle_scan_head",
    "initialize",
    "load_error",
    "__version__",
    # native structs
    "jsScanHeadConfiguration",
    "jsScanHeadCapabilities",
    "jsScanHeadStatus",
    "jsScanSyncStatus",
    "jsExclusionMask",
    "jsBrightnessCorrection_BETA",
    "jsCoordinate",
    "jsProfileData",
    "jsRawProfile",
    "jsCameraImage",
]
