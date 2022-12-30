#include <iostream>
#include <stdexcept>
#include "joescan_pinchot.h"
#include "jsScanApplication.hpp"

/**
 * @brief Display the API version to console output for visual confirmation as
 * to the version being used for this example.
 */
void PrintApiVersion()
{
  uint32_t major, minor, patch;
  jsGetAPISemanticVersion(&major, &minor, &patch);
  std::cout << "Joescan API version " << major << "." << minor << "." << patch
            << std::endl;
}

int main(int argc, char *argv[])
{
  jsScanSystem scan_system = 0;
  jsScanHead scan_head = 0;
  uint32_t serial_number = 0;
  int32_t r = 0;

  if (2 > argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL" << std::endl;
    return 1;
  }

  // Grab the serial number of the scan head from the command line.
  serial_number = strtoul(argv[1], nullptr, 0);

  try {
    PrintApiVersion();

    r = jsPowerCycleScanHead(serial_number);
    if (0 > r) {
      throw joescan::ApiError("failed to power cycle scan head", r);
    }
  } catch (joescan::ApiError &e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    r = 1;

    const char *err_str = nullptr;
    jsError err = e.return_code();
    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  }

  return r;
}
