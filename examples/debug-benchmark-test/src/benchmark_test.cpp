#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif

#include <atomic>
#include <chrono>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "joescan_pinchot.h"
#include "cxxopts.hpp"
#include "jsScanApplication.hpp"

/* NOTE: For Linux, set rmem manually for best results.
   # echo 0x10000000 > /proc/sys/net/core/rmem_max
*/

static std::atomic<bool> _is_scanning(false);
static std::mutex _mtx;
static std::vector<uint64_t> _missing_profiles;
static std::vector<uint64_t> _total_profiles;

// these are included to help avoid the transform from getting optimized out
static volatile double _x = 0.0;
static volatile double _y = 0.0;
static volatile double _z = 0.0;

void PrintStatus(jsScanHeadStatus &stat)
{
  std::cout << "jsScanHeadStatus" << std::endl;
  std::cout << "\tglobal_time_ns=" << stat.global_time_ns << std::endl;
  std::cout << "\tnum_encoder_values=" << stat.num_encoder_values << std::endl;

  std::cout << "\tencoder_values=";
  for (uint32_t n = 0; n < stat.num_encoder_values; n++) {
    std::cout << stat.encoder_values[n];
    if (n != (stat.num_encoder_values - 1)) {
      std::cout << ",";
    } else {
      std::cout << std::endl;
    }
  }

  std::cout << "\tcamera_a_pixels_in_window="
            << stat.camera_a_pixels_in_window << std::endl;
  std::cout << "\tcamera_a_temp=" << stat.camera_a_temp << std::endl;


  std::cout << "\tcamera_b_pixels_in_window="
            << stat.camera_b_pixels_in_window << std::endl;
  std::cout << "\tcamera_b_temp=" << stat.camera_b_temp << std::endl;

  std::cout << "\tnum_profiles_sent=" << stat.num_profiles_sent << std::endl;
}

/**
 * @brief This function receives profile data from a given scan head. We start
 * a thread for each scan head to pull out the data as fast as possible.
 *
 * @param scan_head Reference to the scan head to recieve profile data from.
 */
static void receiver(jsScanHead scan_head)
{
  jsProfile *profiles = nullptr;
  int max_profiles = 10;
  int sleep_ms = 1;

  // Allocate memory to recieve profile data for this scan head.
  profiles = new jsProfile[max_profiles];
  uint32_t serial = jsScanHeadGetSerial(scan_head);
  uint32_t idx = jsScanHeadGetId(scan_head);
  std::map<std::pair<jsCamera, jsLaser>, uint32_t> seq;

  _mtx.lock();
  std::cout << "begin receiving on scan head " << serial << std::endl;
  _mtx.unlock();

  while (_is_scanning) {
    int32_t r = 0;

    r = jsScanHeadWaitUntilProfilesAvailable(scan_head, 10, 100000);
    if (0 > r) {
      std::cout << "ERROR jsScanHeadWaitUntilProfilesAvailable returned" << r
                << std::endl;
      continue;
    } else if (0 == r) {
      continue;
    }

    r = jsScanHeadGetProfiles(scan_head, profiles, max_profiles);
    if (0 > r) {
      std::cout << "ERROR: jsScanHeadGetProfiles returned" << r << std::endl;
      continue;
    } else if (0 == r) {
      std::cout << "ERROR: jsScanHeadGetProfiles no profiles" << std::endl;
      continue;
    }

    _total_profiles[idx] += r;
    for (uint32_t n = 0; n < uint32_t(r); n++) {
      auto pair = std::make_pair(profiles[n].camera, profiles[n].laser);
      if (0 == seq.count(pair)) {
        seq[pair] = 1;
      }

      if (profiles[n].sequence_number == seq[pair]) {
        seq[pair] += 1;
      } else if (profiles[n].sequence_number > seq[pair]) {
        _mtx.lock();
        std::cout << "ERROR: skipped sequence number, got "
                  << profiles[n].sequence_number << ", expected " << seq[pair]
                  << std::endl;
        _mtx.unlock();
        _missing_profiles[idx] += profiles[n].sequence_number - seq[pair];
        seq[pair] = profiles[n].sequence_number + 1;
      } else {
        _mtx.lock();
        std::cout << "ERROR: old sequence number, got "
                  << profiles[n].sequence_number << ", expected " << seq[pair]
                  << std::endl;
        _mtx.unlock();
      }
    }
  }

  _mtx.lock();
  std::cout << "end receiving on scan head " << serial << std::endl;
  _mtx.unlock();

  delete[] profiles;
}

int main(int argc, char *argv[])
{
  jsScanSystem scan_system = 0;
  jsDataFormat fmt = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;

  std::thread *threads = nullptr;
  const int status_message_delay_sec = 1;
  uint64_t scan_time_sec = 10;
  uint32_t laser_min = 25;
  uint32_t laser_def = 25;
  uint32_t laser_max = 25;
  uint32_t period_us = 0;
  double window_top = 20.0;
  double window_bottom = -20.0;
  double window_left = -20.0;
  double window_right = 20.0;
  bool is_status = false;
  std::vector<uint32_t> serial_numbers;
  int32_t r = 0;

  try {
    cxxopts::Options options(argv[0], "scanning benchmark for Joescan C API");
    std::string serials;
    std::string ft;
    std::string lzr;
    std::string prod;
    std::string window;

    options.add_options()(
      "t,time", "Duration in seconds", cxxopts::value<uint64_t>(scan_time_sec))(
      "f,format", "full, half, or quarter", cxxopts::value<std::string>(ft))(
      "l,laser", "usec def or min,def,max", cxxopts::value<std::string>(lzr))(
      "p,period", "scan period in us", cxxopts::value<uint32_t>(period_us))(
      "s,serial", "Serial numbers", cxxopts::value<std::string>(serials))(
      "w,window", "Scan window inches", cxxopts::value<std::string>(window))(
      "status", "Get status while scanning", cxxopts::value<bool>(is_status))(
      "h,help", "Print help");

    auto parsed = options.parse(argc, argv);
    if (parsed.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    if (parsed.count("serial")) {
      std::istringstream ss(serials);
      std::string token;
      while (std::getline(ss, token, ',')) {
        serial_numbers.emplace_back(strtoul(token.c_str(), nullptr, 0));
      }
    } else {
      std::cout << "no serial number(s) provided" << std::endl;
      exit(1);
    }

    if (parsed.count("format")) {
      if ((0 == ft.compare("full")) || (0 == ft.compare("FULL"))) {
        fmt = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;
      } else if ((0 == ft.compare("half")) || (0 == ft.compare("HALF"))) {
        fmt = JS_DATA_FORMAT_XY_BRIGHTNESS_HALF;
      } else if ((0 == ft.compare("quarter")) || (0 == ft.compare("QUARTER"))) {
        fmt = JS_DATA_FORMAT_XY_BRIGHTNESS_QUARTER;
      } else {
        std::cout << "invalid format: " << ft << std::endl;
        exit(1);
      }
    }

    {
      std::istringstream ss(lzr);
      std::string token;
      uint32_t n = 0;

      if (parsed.count("laser")) {
        while (std::getline(ss, token, ',')) {
          if (0 == n) {
            laser_def = strtoul(token.c_str(), nullptr, 0);
            laser_min = laser_def;
            laser_max = laser_def;
          } else if (1 == n) {
            laser_def = strtoul(token.c_str(), nullptr, 0);
            laser_max = laser_def;
          } else if (2 == n) {
            laser_max = strtoul(token.c_str(), nullptr, 0);
          }
          n++;
        }
      }
    }

    {
      std::istringstream ss(window);
      std::string token;
      uint32_t n = 0;

      if (parsed.count("window")) {
        while (std::getline(ss, token, ',')) {
          if (0 == n) {
            window_top = strtod(token.c_str(), nullptr);
          } else if (1 == n) {
            window_bottom = strtod(token.c_str(), nullptr);
          } else if (2 == n) {
            window_left = strtod(token.c_str(), nullptr);
          } else if (3 == n) {
            window_right = strtod(token.c_str(), nullptr);
          }
          n++;
        }
        // use the same parameter for all
        if (4 > n) {
          window_bottom = -1 * window_top;
          window_left = -1 * window_top;
          window_right = window_top;
        }
      }
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }

  try {
    joescan::ScanApplication app;

    _missing_profiles.resize(serial_numbers.size(), 0);
    _total_profiles.resize(serial_numbers.size(), 0);

    app.SetSerialNumber(serial_numbers);
    app.Connect();
    app.SetLaserOn(laser_def, laser_min, laser_max);
    app.SetWindow(window_top, window_bottom, window_left, window_right);
    app.Configure();

    _is_scanning = true;
    app.StartScanning(period_us, fmt, &receiver);

    auto scan_heads = app.GetScanHeads();
    // Periodically print out the number of profiles received.
    for (unsigned long i = 0; i < scan_time_sec; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << i << std::endl;
    }

    _is_scanning = false;
    app.StopScanning();

    for (uint32_t n = 0; n < scan_heads.size(); n++) {
      uint32_t serial = jsScanHeadGetSerial(scan_heads[n]);
      std::cout << serial << ": received " << _total_profiles[n] << ", "
                << _missing_profiles[n] << " missing" << std::endl;
    }

    app.Disconnect();

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

  return 0;
}
