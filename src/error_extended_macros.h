#ifndef _ERROR_EXTENDED_MACROS
#define _ERROR_EXTENDED_MACROS

/// NOTE: In order for these macros to work, the class should define the
/// member variable `std::string m_error_extended_str`.

/// Internal macro, appends file and line number to `error_extended_str`.
#define _ERROR_TRACE \
  (std::string(" [") + __FILE__ + std::string(":") + \
   std::to_string(__LINE__) + std::string("]"))

/// Clears the extended error within a class.
#define CLEAR_ERROR() \
 { m_error_extended_str.clear(); }

/// Exits the function, returning `error_code`, and sets the extended error to
/// to the contents of `error_extended_str`.
#define RETURN_ERROR(error_extended_str, error_code) \
  { \
    m_error_extended_str = (error_extended_str) + _ERROR_TRACE; \
    return (error_code); \
  }

/// Exits the function, returning `nullptr`, and sets the extended error to the
/// contents of `error_extended_str`.
#define RETURN_ERROR_NULLPTR(error_extended_str) \
  { \
    m_error_extended_str = (error_extended_str) + _ERROR_TRACE; \
    return nullptr; \
  }

#endif
