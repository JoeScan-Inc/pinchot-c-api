"""Exception types for the Pinchot Python wrapper."""

from .enums import Error


class PinchotError(Exception):
    """Raised when a native Pinchot API call reports an error.

    :ivar code: The :class:`~joescan_pinchot.enums.Error` value returned by the
        native call (``None`` for errors raised by the wrapper itself).
    :ivar extended: The scan-system / scan-head extended error string when one
        was available, otherwise ``None``.
    """

    def __init__(self, message, code=None, extended=None):
        self.code = code
        self.extended = extended
        parts = [message]
        if code is not None:
            try:
                name = Error(code).name
            except ValueError:
                name = str(code)
            parts.append(f"[jsError {code}: {name}]")
        if extended:
            parts.append(f"({extended})")
        super().__init__(" ".join(parts))
