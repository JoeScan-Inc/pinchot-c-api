"""Constants and enumerations mirroring ``joescan_pinchot.h``.

Every enumeration here has the same integer values as its C counterpart so the
values can be passed straight through to the native library.  The classes
subclass :class:`enum.IntEnum` which means members compare equal to and behave
like plain ``int`` objects.
"""

from enum import IntEnum, IntFlag

# --- Constants (enum jsConstants) -----------------------------------------

#: Maximum string length of JS-50 scan head type string.
JS_SCAN_HEAD_TYPE_STR_MAX_LEN = 32
#: Maximum string length of client network interface name.
JS_CLIENT_NAME_STR_MAX_LEN = 128
#: Maximum number of columns of scan data captured by the camera.
JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN = 1456
#: Array length of data reserved for a profile.
JS_PROFILE_DATA_LEN = JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN
#: Array length of data reserved for a raw profile.
JS_RAW_PROFILE_DATA_LEN = JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN
#: Maximum number of columns in an image taken from the scan head.
JS_CAMERA_IMAGE_DATA_MAX_WIDTH = JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN
#: Maximum number of rows in an image taken from the scan head.
JS_CAMERA_IMAGE_DATA_MAX_HEIGHT = 1088
#: Array length of data reserved for an image.
JS_CAMERA_IMAGE_DATA_LEN = (
    JS_CAMERA_IMAGE_DATA_MAX_HEIGHT * JS_CAMERA_IMAGE_DATA_MAX_WIDTH
)
#: Value assigned to ``x``/``y`` in profile data when the point is invalid.
JS_PROFILE_DATA_INVALID_XY = -2147483648  # INT_MIN
#: Value assigned to ``brightness`` when the measurement is invalid.
JS_PROFILE_DATA_INVALID_BRIGHTNESS = 0
#: Maximum number of profiles that can be read with one API call.
JS_SCAN_HEAD_PROFILES_MAX = 1000
#: Invalid serial number of a JS-50 scan head.
JS_SCAN_HEAD_INVALID_SERIAL = 0
#: Invalid serial number of a ScanSync.
JS_SCANSYNC_INVALID_SERIAL = 0
#: Invalid encoder value from ScanSync.
JS_SCANSYNC_INVALID_ENCODER = 9223372036854775807  # INT64_MAX


class Error(IntEnum):
    """Enumerated value for possible errors returned from API functions."""

    NONE = 0
    INTERNAL = -1
    NULL_ARGUMENT = -2
    INVALID_ARGUMENT = -3
    NOT_CONNECTED = -4
    CONNECTED = -5
    NOT_SCANNING = -6
    SCANNING = -7
    VERSION_COMPATIBILITY = -8
    ALREADY_EXISTS = -9
    NO_MORE_ROOM = -10
    NETWORK = -11
    NOT_DISCOVERED = -12
    USE_CAMERA_FUNCTION = -13
    USE_LASER_FUNCTION = -14
    FRAME_SCANNING = -15
    NOT_FRAME_SCANNING = -16
    FRAME_SCANNING_INVALID_PHASE_TABLE = -17
    PHASE_TABLE_EMPTY = -18
    DEPRECATED = -19
    INVALID_SCAN_SYSTEM = -20
    INVALID_SCAN_HEAD = -21
    UNKNOWN = -22


class Units(IntEnum):
    """Units used for configuration and returned data."""

    INVALID = 0
    INCHES = 1
    MILLIMETER = 2


class CableOrientation(IntEnum):
    """Camera cable orientation for a scan head."""

    INVALID = 0
    DOWNSTREAM = 1
    UPSTREAM = 2


class ScanWindowType(IntEnum):
    """Scan head window type."""

    INVALID = 0
    UNCONSTRAINED = 1
    RECTANGULAR = 2
    POLYGONAL = 3


class ScanHeadType(IntEnum):
    """Scan head product type."""

    INVALID = 0
    JS50WX = 1
    JS50WSC = 2
    JS50X6B20 = 3
    JS50X6B30 = 4
    JS50MX = 5
    JS50Z820 = 6
    JS50Z830 = 7


class Camera(IntEnum):
    """Identifies a camera on the scan head."""

    INVALID = 0
    A = 1
    B = 2
    MAX = 3


class Laser(IntEnum):
    """Identifies a laser on the scan head."""

    INVALID = 0
    L1 = 1
    L2 = 2
    L3 = 3
    L4 = 4
    L5 = 5
    L6 = 6
    L7 = 7
    L8 = 8
    MAX = 9


class Encoder(IntEnum):
    """Identifies an encoder / indexes ``encoder_values`` arrays."""

    MAIN = 0
    AUX_1 = 1
    AUX_2 = 2
    MAX = 3


class ProfileFlags(IntFlag):
    """Bitmask flags reported in a profile's ``flags`` field."""

    ENCODER_MAIN_FAULT_A = 1 << 0
    ENCODER_MAIN_FAULT_B = 1 << 1
    ENCODER_MAIN_FAULT_Y = 1 << 2
    ENCODER_MAIN_FAULT_Z = 1 << 3
    ENCODER_MAIN_OVERRUN = 1 << 4
    ENCODER_MAIN_TERMINATION_ENABLE = 1 << 5
    ENCODER_MAIN_INDEX_Z = 1 << 6
    ENCODER_MAIN_SYNC = 1 << 7
    ENCODER_MAIN_AUX_Y = 1 << 8
    ENCODER_MAIN_FAULT_SYNC = 1 << 9
    ENCODER_MAIN_LASER_DISABLE = 1 << 10
    ENCODER_MAIN_FAULT_LASER_DISABLE = 1 << 11


class DataFormat(IntEnum):
    """Types of data and the resolution it can take."""

    INVALID = 0
    XY_BRIGHTNESS_FULL = 1
    XY_BRIGHTNESS_HALF = 2
    XY_BRIGHTNESS_QUARTER = 3
    XY_FULL = 4
    XY_HALF = 5
    XY_QUARTER = 6


class DiagnosticMode(IntEnum):
    """Fixed or auto exposure when obtaining diagnostic profiles/images."""

    INVALID = 0
    FIXED_EXPOSURE = 1
    AUTO_EXPOSURE = 2


class ScanHeadState(IntEnum):
    """Current state of a scan head."""

    INVALID = 0
    STANDBY = 1
    CONNECTED = 2
    SCANNING = 3
    SCANNING_IDLE = 4
