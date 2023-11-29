#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#if defined _MSC_VER
#include <direct.h>
#elif defined __GNUC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "joescan_pinchot.h"
#include "cxxopts.hpp"
#include "jsScanApplication.hpp"

/* NOTE: For Linux, set rmem manually for best results.
   # echo 0x10000000 > /proc/sys/net/core/rmem_max
*/

/** @brief Set this define to skip logging profile data to file. */
//#define _SKIP_PROFILE_DATA 0

static std::mutex _lock;
static jsProfile *_profiles = nullptr;
static uint32_t _num_profiles_received = 0;
static uint32_t _num_profiles_requested = 0;
static bool _is_running = true;

/**
 * @brief This function receives profile data from a given scan head. We start
 * a thread for each scan head to pull out the data as fast as possible.
 *
 * @param scan_head Reference to the scan head to receive profile data from.
 */
static void receiver(jsScanHead scan_head)
{
  int num_profiles_remaining = 0;
  int max_profiles = 100;
  uint32_t timeout_us = 100000;

  num_profiles_remaining = _num_profiles_requested;

  while (_is_running && (0 != num_profiles_remaining)) {
    int num_profiles = std::min(max_profiles, num_profiles_remaining);
    unsigned int n = _num_profiles_received;

    jsScanHeadWaitUntilProfilesAvailable(scan_head, num_profiles, timeout_us);

    auto r = jsScanHeadGetProfiles(scan_head, &_profiles[n], num_profiles);
    if (0 < r) {
      std::lock_guard<std::mutex> guard(_lock);
      _num_profiles_received += r;
      num_profiles_remaining -= r;
    }
  }

  {
    std::lock_guard<std::mutex> guard(_lock);
    std::cout << "receive thread done" << std::endl;
  }
}

static uint32_t count_digits(uint32_t n)
{
  uint32_t count = 0;

  while (n != 0) {
    n = n / 10;
    ++count;
  }

  return count;
}

/**
 * @brief This function takes the received profile data and writes it to the
 * file name specified on the command line.
 *
 * @param out_file_name The file name as a string to store the profiles to.
 */
static void saver_bin(std::string out_file_name)
{
  std::ofstream f;
  int num_profiles_remaining = 0;
  unsigned int n = 0;
  int sleep_ms = 1;

  f.open(out_file_name + ".bin", std::ios::out | std::ios::binary);
  num_profiles_remaining = _num_profiles_requested;

  while (_is_running && (0 != num_profiles_remaining)) {
    while (n < _num_profiles_received) {
      auto *p = &_profiles[n];
      f.write((char *) p, sizeof(jsProfile));
      num_profiles_remaining--;
      n++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  _is_running = false;
  f.close();
}

static void saver_csv(std::string out_file_name)
{
  std::ofstream f;
  int num_profiles_remaining = 0;
  unsigned int n = 0;
  int sleep_ms = 1;
  const uint32_t n_zero = count_digits(_num_profiles_requested);

  num_profiles_remaining = _num_profiles_requested;

  int r = 0;
#if defined _MSC_VER
  r = _mkdir(out_file_name.c_str());
#elif defined __GNUC__
  r = mkdir(out_file_name.c_str(), 0777);
#endif

  if (0 != r) {
    std::cout << "failed to create directory" << std::endl;
#if defined __GNUC__
    std::cout << strerror(errno) << std::endl;
#endif
    _is_running = false;
  }

  while (_is_running && (0 != num_profiles_remaining)) {
    while (n < _num_profiles_received) {
      auto *p = &_profiles[n];

      std::string tmp = std::to_string(n);
      tmp = std::string(n_zero - tmp.length(), '0') + tmp;
      tmp = tmp + "-camera" + std::to_string(p->camera) + "-laser" +
            std::to_string(p->laser);

      f.open(out_file_name + "/" + tmp + ".csv");

      for (unsigned int i = 0; i < p->data_len; i++) {
        f << p->data[i].x << "," << p->data[i].y << "\n";
      }

      f.close();
      num_profiles_remaining--;
      n++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  _is_running = false;
}

/**
 * @brief This function takes the received profile data and writes it to the
 * file name specified on the command line.
 *
 * @param out_file_name The file name as a string to store the profiles to.
 */
static void saver_txt(std::string out_file_name)
{
  std::ofstream f;
  int num_profiles_remaining = 0;
  unsigned int n = 0;
  int sleep_ms = 1;

  f.open(out_file_name + ".txt");
  num_profiles_remaining = _num_profiles_requested;

  while (_is_running && (0 != num_profiles_remaining)) {
    while (n < _num_profiles_received) {
      auto *p = &_profiles[n];
      f << "[profile " << n << "]\n";
      f << "\tscan_head_id: " << p->scan_head_id << "\n";
      f << "\tcamera: " << p->camera << "\n";
      f << "\tlaser: " << p->laser << "\n";
      f << "\ttimestamp_ns: " << p->timestamp_ns << "\n";
      f << "\tflags: " << p->flags << "\n";
      f << "\tsequence number: " << p->sequence_number << "\n";
      f << "\tnum_encoder_values: " << p->num_encoder_values << "\n";

      f << "\tencoder_values: " << p->encoder_values[JS_ENCODER_MAIN] << " "
        << p->encoder_values[JS_ENCODER_AUX_1] << " "
        << p->encoder_values[JS_ENCODER_AUX_2] << " "
        << "\n";

      f << "\tlaser_on_time_us: " << p->laser_on_time_us << "\n";
      f << "\tformat: " << p->format << std::endl;
      f << "\tpackets_received: " << p->packets_received << "\n";
      f << "\tpackets_expected: " << p->packets_expected << "\n";
      f << "\tdata_len: " << p->data_len << "\n";

#ifndef _SKIP_PROFILE_DATA
      f << "\tdata:\n";
      for (unsigned int i = 0; i < p->data_len; i++) {
        f << "\t\t[" << i << "] { ";
        f << "x : " << p->data[i].x << ", ";
        f << "y : " << p->data[i].y << ", ";
        f << "brightness : " << p->data[i].brightness << " }\n";
      }
#endif
      num_profiles_remaining--;
      n++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  _is_running = false;
  f.close();
}

int main(int argc, char *argv[])
{
  jsScanSystem scan_system = 0;
  jsDataFormat fmt = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;

  std::thread thread_save;
  std::string out_name = "out";
  uint32_t num_profiles = 10;
  uint32_t laser_min = 25;
  uint32_t laser_def = 25;
  uint32_t laser_max = 25;
  uint32_t period_us = 0;
  uint32_t thresh = 1;
  double window_top = 20.0;
  double window_bottom = -20.0;
  double window_left = -20.0;
  double window_right = 20.0;
  double roll = 0.0;
  bool is_out_bin = false;
  bool is_out_csv = false;
  uint32_t serial = 0;
  int32_t r = 0;

  try {
    cxxopts::Options options(argv[0], "capture and save jsProfile data");
    std::string ft;
    std::string lzr;
    std::string window;

    options.add_options()(
      "f,format", "Data format (full/half)", cxxopts::value<std::string>(ft))(
      "l,laser", "usec def or min,def,max", cxxopts::value<std::string>(lzr))(
      "n,count", "Number of profiles", cxxopts::value<uint32_t>(num_profiles))(
      "o,out", "Output file/directory", cxxopts::value<std::string>(out_name))(
      "s,serial", "Serial number", cxxopts::value<std::uint32_t>(serial))(
      "threshold", "Laser detect threshold", cxxopts::value<uint32_t>(thresh))(
      "w,window", "Scan window inches", cxxopts::value<std::string>(window))(
      "bin", "Output jsProfile binary file", cxxopts::value<bool>(is_out_bin))(
      "csv", "Output X/Y CSV file", cxxopts::value<bool>(is_out_csv))(
      "roll", "Set alignment roll", cxxopts::value<double>(roll))(
      "h,help", "Print help");

    auto parsed = options.parse(argc, argv);
    if (parsed.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
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

      if (is_out_bin && is_out_csv) {
        std::cout << "can't save binary and csv" << std::endl;
        exit(1);
      }
    }

  } catch (const cxxopts::OptionException &e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    exit(1);
  }

  try {
    joescan::ScanApplication app;

    app.SetSerialNumber(serial);
    app.Connect();
    app.SetLaserOn(laser_def, laser_min, laser_max);
    app.SetThreshold(thresh);
    app.SetWindow(window_top, window_bottom, window_left, window_right);
    app.Configure();

    std::cout << "acquiring " << num_profiles << " profiles" << std::endl;
    _num_profiles_requested = num_profiles;
    _num_profiles_received = 0;
    _profiles = new jsProfile[num_profiles];

    app.StartScanning(period_us, fmt, &receiver);

    // And another thread for writing data out to file.
    if (is_out_bin) {
      thread_save = std::thread(saver_bin, out_name);
    } else if (is_out_csv) {
      thread_save = std::thread(saver_csv, out_name);
    } else {
      thread_save = std::thread(saver_txt, out_name);
    }

    // Periodically print out the number of profiles recieved.
    while (_is_running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      {
        std::lock_guard<std::mutex> guard(_lock);
        std::cout << _num_profiles_received << std::endl;
      }
    }

    app.StopScanning();
    thread_save.join();
    app.Disconnect();

    r = 0;
  } catch (joescan::ApiError &e) {
    e.print();
    r = 1;
  }

  if (nullptr != _profiles) {
    delete _profiles;
  }

  return r;
}
