/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "joescan_pinchot.h"
#include "FlatbufferMessages.hpp"
#include "NetworkInterface.hpp"
#include "ProfileQueue.hpp"
#include "RawProfileToProfile.hpp"
#include "ScanHead.hpp"
#include "ScanManager.hpp"
#include "ScanSyncManager.hpp"
#include "Version.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <cassert>
#include <cstring>
#include <cmath>

using namespace joescan;

static std::map<uint32_t, ScanManager*> _uid_to_scan_manager;
// memory for storing extended error when user reads it out
static std::string _last_error_extended_str;
static ScanSyncManager _scansync;

static ScanManager *_get_scan_manager_object(jsScanSystem scan_system)
{
  uint32_t uid = scan_system & 0xFFFFFFFF;

  if (_uid_to_scan_manager.find(uid) == _uid_to_scan_manager.end()) {
    return nullptr;
  }

  ScanManager *m = _uid_to_scan_manager[uid];

  return m;
}

static ScanHead *_get_scan_head_object(jsScanHead scan_head)
{
  // upper 32 bytes is ScanManager UID
  ScanManager *m = _get_scan_manager_object((scan_head >> 32) & 0xFFFFFFFF);
  if (nullptr == m) {
    return nullptr;
  }

  // lower 32 bytes is ScanHead serial
  ScanHead *h = m->GetScanHeadBySerial(scan_head & 0xFFFFFFFF);
  if (nullptr == h) {
    return nullptr;
  }

  return h;
}

static jsScanSystem _get_jsScanSystem(ScanManager *manager)
{
  uint32_t uid = manager->GetUID();
  jsScanSystem ss = uid;

  return ss;
}

static jsScanHead _get_jsScanHead(ScanHead *scan_head)
{
  ScanManager &manager = scan_head->GetScanManager();
  uint32_t serial = scan_head->GetSerialNumber();

  jsScanSystem ss = _get_jsScanSystem(&manager);
  // upper 32 bytes is ScanManager UID
  // lower 32 bytes is ScanHead serial
  jsScanHead sh = (ss << 32) | serial;

  return sh;
}

EXPORTED
void jsGetAPIVersion(const char **version_str)
{
  *version_str = API_VERSION_FULL;
}

EXPORTED
void jsGetAPISemanticVersion(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
  if (nullptr != major) {
    *major = API_VERSION_MAJOR;
  }
  if (nullptr != minor) {
    *minor = API_VERSION_MINOR;
  }
  if (nullptr != patch) {
    *patch = API_VERSION_PATCH;
  }
}

EXPORTED
void jsGetError(int32_t return_code, const char **error_str)
{
  if (0 <= return_code) {
    *error_str = "none";
  } else {
    switch (return_code) {
      case (JS_ERROR_INTERNAL):
        *error_str = "internal error";
        break;
      case (JS_ERROR_NULL_ARGUMENT):
        *error_str = "null value argument";
        break;
      case (JS_ERROR_INVALID_ARGUMENT):
        *error_str = "invalid argument";
        break;
      case (JS_ERROR_NOT_CONNECTED):
        *error_str = "state not connected";
        break;
      case (JS_ERROR_CONNECTED):
        *error_str = "state connected";
        break;
      case (JS_ERROR_NOT_SCANNING):
        *error_str = "state not scanning";
        break;
      case (JS_ERROR_SCANNING):
        *error_str = "state scanning";
        break;
      case (JS_ERROR_VERSION_COMPATIBILITY):
        *error_str = "versions not compatible";
        break;
      case (JS_ERROR_ALREADY_EXISTS):
        *error_str = "already exists";
        break;
      case (JS_ERROR_NO_MORE_ROOM):
        *error_str = "no more room";
        break;
      case (JS_ERROR_NETWORK):
        *error_str = "network error";
        break;
      case (JS_ERROR_NOT_DISCOVERED):
        *error_str = "scan head not discovered on network";
        break;
      case (JS_ERROR_USE_CAMERA_FUNCTION):
        *error_str = "wrong function called, use Camera variant function";
        break;
      case (JS_ERROR_USE_LASER_FUNCTION):
        *error_str = "wrong function called, use Laser variant function";
        break;
      case (JS_ERROR_FRAME_SCANNING):
        *error_str = "not supported with frame scanning";
        break;
      case (JS_ERROR_NOT_FRAME_SCANNING):
        *error_str = "only supported with frame scanning";
        break;
      case (JS_ERROR_FRAME_SCANNING_INVALID_PHASE_TABLE):
        *error_str = "phase table not compatible with frame scanning";
        break;
      case (JS_ERROR_PHASE_TABLE_EMPTY):
        *error_str = "phase table empty";
        break;
      case (JS_ERROR_DEPRECATED):
        *error_str = "deprecated feature";
      case (JS_ERROR_INVALID_SCAN_SYSTEM):
        *error_str = "invalid scan system reference";
        break;
      case (JS_ERROR_INVALID_SCAN_HEAD):
        *error_str = "invalid scan head reference";
        break;
      case (JS_ERROR_UNKNOWN):
      default:
        *error_str = "unknown error";
    }
  }
}

EXPORTED
int32_t jsScanSystemGetLastErrorExtended(
  jsScanSystem scan_system,
  const char **error_extended_str)
{
  int r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    _last_error_extended_str = manager->GetErrorExtended();
    *error_extended_str = _last_error_extended_str.c_str();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetLastErrorExtended(
  jsScanHead scan_head,
  const char **error_extended_str)
{
  int r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    _last_error_extended_str = sh->GetErrorExtended();
    *error_extended_str = _last_error_extended_str.c_str();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
void jsProfileInit(jsProfile *profile)
{
  if (nullptr == profile) {
    return;
  }

  profile->timestamp_ns = 0;
  profile->format = JS_DATA_FORMAT_INVALID;
  profile->data_len = 0;
}

EXPORTED
void jsRawProfileInit(jsRawProfile *profile)
{
  if (nullptr == profile) {
    return;
  }

  profile->timestamp_ns = 0;
  profile->format = JS_DATA_FORMAT_INVALID;
  profile->data_len = 0;
  profile->data_valid_brightness = 0;
  profile->data_valid_xy = 0;
}

EXPORTED
int32_t jsPowerCycleScanHead(uint32_t serial_number)
{
  constexpr uint16_t kUpdatePort = 21232;
  std::unique_ptr<TCPSocket> tcp;
  int32_t r = 0;

  try {
    std::unique_ptr<ScanManager> manager(
      new ScanManager(JS_UNITS_INCHES, &_scansync));
    r = manager->Discover();
    if (0 > r) {
      return r;
    } else if (0 == r) {
      return JS_ERROR_NOT_DISCOVERED;
    }

    std::vector<jsDiscovered> discovered;
    discovered.resize(r);
    r = manager->ScanHeadsDiscovered(&discovered[0], r);
    if (0 > r) {
      return r;
    }

    uint32_t idx = 0;
    for (uint32_t n = 0; n < discovered.size(); n++) {
      // First check if the head can be discovered.
      if (discovered[n].serial_number == serial_number) {
        break;
      }
      idx++;
    }

    if (discovered.size() == idx) {
      // Failed to find in BroadcastDiscover, try again using MDNS
      uint32_t ip_addr;
      r = NetworkInterface::ResolveIpAddressMDNS(serial_number, &ip_addr);
      if (0 != r) {
        return JS_ERROR_NOT_DISCOVERED;
      }
      // MDNS doesn't give us the network interface it was discovered on. We'll
      // have to hope the operating system can route correctly.
      tcp = std::unique_ptr<TCPSocket>(
              new TCPSocket(ip_addr, kUpdatePort, 10));
    } else {
      // Scan head was discovered and we know what network interface to use.
      tcp = std::unique_ptr<TCPSocket>(
              new TCPSocket(discovered[idx].client_name_str,
                            discovered[idx].client_ip_addr,
                            discovered[idx].ip_addr,
                            kUpdatePort,
                            10));
    }

    using namespace joescan::schema::update::client;
    flatbuffers::FlatBufferBuilder builder(0x20);

    builder.Clear();
    auto msg_offset = CreateMessageClient(builder, MessageType_REBOOT_REQUEST,
                                          MessageData_NONE);
    builder.Finish(msg_offset);

    r = tcp->Send(builder);
    if (0 > r) {
      return r;
    }
    // Delay here to ensure the non-blocking TCP write fully finishes before
    // the TCPSocket class goes out of scope and is destroyed.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return 0;
}

EXPORTED
jsScanSystem jsScanSystemCreate(jsUnits units)
{
  jsScanSystem scan_system;

  if (JS_UNITS_INCHES != units && JS_UNITS_MILLIMETER != units) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  try {
    ScanManager *manager = new ScanManager(units, &_scansync);
    _uid_to_scan_manager[manager->GetUID()] = manager;
    scan_system = _get_jsScanSystem(manager);
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return scan_system;
}

EXPORTED
void jsScanSystemFree(jsScanSystem scan_system)
{
  try {
    if (jsScanSystemIsScanning(scan_system)) {
      jsScanSystemStopScanning(scan_system);
    }

    if (jsScanSystemIsConnected(scan_system)) {
      jsScanSystemDisconnect(scan_system);
    }

    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return;
    }

    _uid_to_scan_manager.erase(manager->GetUID());
    delete manager;
  } catch (std::exception &e) {
    (void)e;
  }
}

EXPORTED
int jsScanSystemDiscover(jsScanSystem scan_system)
{
  int r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->Discover();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemGetDiscovered(jsScanSystem scan_system,
                              jsDiscovered *results, uint32_t max_results)
{
  int r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->ScanHeadsDiscovered(results, max_results);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemScanSyncDiscover(
  jsScanSystem scan_system)
{
  int r = 0;

  try {
    r = _scansync.GetDiscoveredSize();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemGetScanSyncDiscovered(
  jsScanSystem scan_system,
  jsScanSyncDiscovered *results,
  uint32_t max_results)
{
  int r = 0;

  try {
    auto discovered = _scansync.GetDiscovered();
    for (uint32_t n = 0; (n < discovered.size()) && (n < max_results); n++) {
      results[n] = discovered[n];
    }
    r = (int) discovered.size();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemSetScanSyncEncoder(
  jsScanSystem scan_system,
  uint32_t serial_main,
  uint32_t serial_aux1,
  uint32_t serial_aux2)
{
  int r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->SetScanSyncEncoder(serial_main,
                                    serial_aux1,
                                    serial_aux2);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemGetScanSyncEncoder(
  jsScanSystem scan_system,
  uint32_t *serial_main,
  uint32_t *serial_aux1,
  uint32_t *serial_aux2)
{
  int r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->GetScanSyncEncoder(serial_main,
                                    serial_aux1,
                                    serial_aux2);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemGetScanSyncStatus(
  jsScanSystem scan_system,
  uint32_t serial,
  jsScanSyncStatus *status)
{
  int r = 0;

  try {
    r = _scansync.GetStatus(serial, status);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int jsScanSystemGetEncoder(jsScanSystem scan_system, jsEncoder encoder,
                           int64_t *value)
{
  return JS_ERROR_DEPRECATED;
}

EXPORTED
jsScanHead jsScanSystemCreateScanHead(jsScanSystem scan_system, uint32_t serial,
                                      uint32_t id)
{
  jsScanHead scan_head = 0;
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    if (true == manager->IsConnected()) {
      return JS_ERROR_CONNECTED;
    }

    r = manager->CreateScanHead(serial, id);
    if (0 != r) {
      return r;
    }

    ScanHead *s = manager->GetScanHeadBySerial(serial);
    scan_head = _get_jsScanHead(s);
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return scan_head;
}

EXPORTED
jsScanHead jsScanSystemGetScanHeadById(jsScanSystem scan_system, uint32_t id)
{
  jsScanHead scan_head = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *s = manager->GetScanHeadById(id);
    if (nullptr == s) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    scan_head = _get_jsScanHead(s);
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return scan_head;
}

EXPORTED
jsScanHead jsScanSystemGetScanHeadBySerial(jsScanSystem scan_system,
                                           uint32_t serial)
{
  jsScanHead scan_head = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *s = manager->GetScanHeadBySerial(serial);
    if (nullptr == s) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    scan_head = _get_jsScanHead(s);
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return scan_head;
}

EXPORTED
int32_t jsScanSystemGetNumberScanHeads(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    uint32_t sz = manager->GetNumberScanners();
    r = static_cast<int32_t>(sz);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

#if 0
// NOTE: These functions were determined not be necessary for the customer API
// at this point. Can be added back into the API in the future.
/**
 * @brief Removes a scan head from being managed by a given `jsScanSystem`.
 *
 * @param scan_system Reference to system that owns the scan head.
 * @param scan_head Reference to scan head to be removed.
 * @return `0` on success, negative value on error.
 */
EXPORTED
int32_t jsScanSystemRemoveScanHead(jsScanSystem scan_system,
  jsScanHead scan_head);

/**
 * @brief Removes a scan head from being managed by a given `jsScanSystem`.
 *
 * @param scan_system Reference to system that owns the scan head.
 * @param id The id of the scan head to remove.
 * @return `0` on success, negative value on error.
 */
EXPORTED
int32_t jsScanSystemRemoveScanHeadById(jsScanSystem scan_system, uint32_t id);

/**
 * @brief Removes a scan head from being managed by a given `jsScanSystem`.
 *
 * @param scan_system Reference to system that owns the scan head.
 * @param serial The serial number of the scan head to remove.
 * @return `0` on success, negative value on error.
 */
EXPORTED
int32_t jsScanSystemRemoveScanHeadBySerial(jsScanSystem scan_system,
  uint32_t serial);

/**
 * @brief Removes all scan heads being managed by a given `jsScanSystem`.
 *
 * @param scan_system Reference to system of scan heads.
 * @return `0` on success, negative value on error.
 */
EXPORTED
int32_t jsScanSystemRemoveAllScanHeads(jsScanSystem scan_system);


EXPORTED
int32_t jsScanSystemRemoveScanHeadById(jsScanSystem scan_system, uint32_t id)
{
  int32_t r = 0;

  try {
    ScanManager *manager = static_cast<ScanManager*>(scan_system);
    ScanHead *s = manager->GetScanHead(id);
    if (nullptr == s) {
      r = JS_ERROR_INVALID_ARGUMENT;
    }
    else {
      manager->RemoveScanHead(s);
    }
  } catch (std::exception &e) {
    (void) e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemRemoveScanHeadBySerial(jsScanSystem scan_system,
  uint32_t serial)
{
  int32_t r = 0;

  try {
    ScanManager *manager = static_cast<ScanManager*>(scan_system);
    manager->RemoveScanHead(serial);
  } catch (std::exception &e) {
    (void) e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemRemoveAllScanHeads(jsScanSystem scan_system)
{
  int32_t r;

  try {
    ScanManager *manager = static_cast<ScanManager*>(scan_system);
    manager->RemoveAllScanHeads();
    r = 0;
  } catch (std::exception &e) {
    (void) e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}
#endif

EXPORTED
int32_t jsScanSystemConnect(jsScanSystem scan_system, int32_t timeout_s)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->Connect(timeout_s);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemDisconnect(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->Disconnect();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
bool jsScanSystemIsConnected(jsScanSystem scan_system)
{
  bool is_connected = false;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return false;
    }

    is_connected = manager->IsConnected();
  } catch (std::exception &e) {
    (void)e;
    is_connected = false;
  }

  return is_connected;
}

EXPORTED
int32_t jsScanSystemPhaseClearAll(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->PhaseClearAll();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseCreate(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->PhaseCreate();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseInsertCamera(jsScanSystem scan_system,
                                      jsScanHead scan_head, jsCamera camera)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = manager->PhaseInsert(sh, camera);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseInsertLaser(jsScanSystem scan_system,
                                     jsScanHead scan_head, jsLaser laser)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = manager->PhaseInsert(sh, laser);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseInsertConfigurationCamera(jsScanSystem scan_system,
                                                   jsScanHead scan_head,
                                                   jsScanHeadConfiguration *cfg,
                                                   jsCamera camera)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = manager->PhaseInsert(sh, camera, cfg);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseInsertConfigurationLaser(jsScanSystem scan_system,
                                                  jsScanHead scan_head,
                                                  jsScanHeadConfiguration *cfg,
                                                  jsLaser laser)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = manager->PhaseInsert(sh, laser, cfg);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemPhaseInsertCameraConfiguration(jsScanSystem scan_system,
                                                   jsScanHead scan_head,
                                                   jsCamera camera,
                                                   jsScanHeadConfiguration cfg)
{
  return JS_ERROR_DEPRECATED;
}

EXPORTED
int32_t jsScanSystemPhaseInsertLaserConfiguration(jsScanSystem scan_system,
                                                  jsScanHead scan_head,
                                                  jsLaser laser,
                                                  jsScanHeadConfiguration cfg)
{
  return JS_ERROR_DEPRECATED;
}

EXPORTED
int32_t jsScanSystemGetMinScanPeriod(jsScanSystem scan_system)
{
  uint32_t period_us = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    if (!manager->IsConnected()) {
      return JS_ERROR_NOT_CONNECTED;
    }

    period_us = manager->GetMinScanPeriod();
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return (int32_t)period_us;
}

EXPORTED
bool jsScanSystemIsConfigured(
  jsScanSystem scan_system)
{
  bool is_configured = false;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return false;
    }

    is_configured = manager->IsConfigured();
  } catch (std::exception &e) {
    (void)e;
    return false;
  }

  return is_configured;
}

EXPORTED
int32_t jsScanSystemConfigure(
  jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->Configure();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemStartScanning(jsScanSystem scan_system, uint32_t period_us,
                                  jsDataFormat fmt)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->StartScanning(period_us, fmt);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemStopScanning(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->StopScanning();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemStartFrameScanning(
  jsScanSystem scan_system,
  uint32_t period_us,
  jsDataFormat fmt)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->StartScanning(period_us, fmt, true);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemGetProfilesPerFrame(
  jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->GetProfilesPerFrame();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemWaitUntilFrameAvailable(
  jsScanSystem scan_system,
  uint32_t timeout_us)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->WaitUntilFrameAvailable(timeout_us);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
bool jsScanSystemIsFrameAvailable(
  jsScanSystem scan_system)
{
  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr != manager) {
      int32_t r = manager->WaitUntilFrameAvailable(0);
      if (0 < r) {
        return true;
      }
    }
  } catch (std::exception &e) {
    (void)e;
  }

  return false;
}

EXPORTED
int32_t jsScanSystemGetFrame(
  jsScanSystem scan_system,
  jsProfile *profiles)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->GetFrame(profiles);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemClearFrames(
  jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->ClearFrames();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemGetRawFrame(
  jsScanSystem scan_system,
  jsRawProfile *profiles)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->GetFrame(profiles);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
bool jsScanSystemIsScanning(jsScanSystem scan_system)
{
  bool is_scanning = false;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return false;
    }

    is_scanning = manager->IsScanning();
  } catch (std::exception &e) {
    (void)e;
    is_scanning = false;
  }

  return is_scanning;
}

EXPORTED
jsScanHeadType jsScanHeadGetType(jsScanHead scan_head)
{
  jsScanHeadType type = JS_SCAN_HEAD_INVALID_TYPE;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_SCAN_HEAD_INVALID_TYPE;
    }

    type = sh->GetType();
  } catch (std::exception &e) {
    (void)e;
    type = JS_SCAN_HEAD_INVALID_TYPE;
  }

  return type;
}

EXPORTED
uint32_t jsScanHeadGetId(jsScanHead scan_head)
{
  uint32_t id = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      // make it super obvious that the ID is invalid
      return UINT_MAX;
    }

    id = sh->GetId();
  } catch (std::exception &e) {
    (void)e;
    id = UINT_MAX;
  }

  return id;
}

EXPORTED
uint32_t jsScanHeadGetSerial(jsScanHead scan_head)
{
  uint32_t serial = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      // make it super obvious that the serial is invalid
      return UINT_MAX;
    }

    serial = sh->GetSerialNumber();
  } catch (std::exception &e) {
    (void)e;
    serial = UINT_MAX;
  }

  return serial;
}

EXPORTED
int32_t jsScanHeadGetCapabilities(jsScanHead scan_head,
                                  jsScanHeadCapabilities *capabilities)
{
  int32_t r = 0;

  try {
    if (nullptr == capabilities) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *capabilities = sh->GetCapabilities();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetFirmwareVersion(jsScanHead scan_head,
                                           uint32_t *major, uint32_t *minor,
                                           uint32_t *patch)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    auto version = sh->GetFirmwareVersion();
    *major = version.major;
    *minor = version.minor;
    *patch = version.patch;
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetConfiguration(jsScanHead scan_head,
                                   jsScanHeadConfiguration *cfg)
{
  int32_t r = 0;

  try {
    if (nullptr == cfg) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetConfiguration(*cfg);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetConfiguration(jsScanHead scan_head,
                                   jsScanHeadConfiguration *cfg)
{
  int32_t r = 0;

  try {
    if (nullptr == cfg) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *cfg = sh->GetConfiguration();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetConfigurationDefault(jsScanHead scan_head,
                                          jsScanHeadConfiguration *cfg)
{
  int32_t r = 0;

  try {
    if (nullptr == cfg) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *cfg = sh->GetConfigurationDefault();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetCableOrientation(jsScanHead scan_head,
                                      jsCableOrientation cable)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetCableOrientation(cable);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetCableOrientation(jsScanHead scan_head,
                                      jsCableOrientation *cable)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *cable = sh->GetCableOrientation();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetAlignment(jsScanHead scan_head, double roll_degrees,
                               double shift_x, double shift_y)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetAlignment(roll_degrees, shift_x, shift_y);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetAlignmentCamera(jsScanHead scan_head, jsCamera camera,
                                     double roll_degrees, double shift_x,
                                     double shift_y)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetAlignment(camera, roll_degrees, shift_x, shift_y);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetAlignmentCamera(jsScanHead scan_head, jsCamera camera,
                                     double *roll_degrees, double *shift_x,
                                     double *shift_y)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetAlignment(camera, roll_degrees, shift_x, shift_y);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetAlignmentLaser(jsScanHead scan_head, jsLaser laser,
                                    double roll_degrees, double shift_x,
                                    double shift_y)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetAlignment(laser, roll_degrees, shift_x, shift_y);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetAlignmentLaser(jsScanHead scan_head, jsLaser laser,
                                    double *roll_degrees, double *shift_x,
                                    double *shift_y)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetAlignment(laser, roll_degrees, shift_x, shift_y);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetExclusionMaskCamera(
  jsScanHead scan_head,
  jsCamera camera,
  jsExclusionMask *mask)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetExclusionMask(camera, mask);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetExclusionMaskLaser(
  jsScanHead scan_head,
  jsLaser laser,
  jsExclusionMask *mask)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetExclusionMask(laser, mask);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetExclusionMaskCamera(
  jsScanHead scan_head,
  jsCamera camera,
  jsExclusionMask *mask)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetExclusionMask(camera, mask);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetExclusionMaskLaser(
  jsScanHead scan_head,
  jsLaser laser,
  jsExclusionMask *mask)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetExclusionMask(laser, mask);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetBrightnessCorrectionCamera_BETA(
  jsScanHead scan_head,
  jsCamera camera,
  jsBrightnessCorrection_BETA *correction)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetBrightnessCorrection(camera, correction);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetBrightnessCorrectionLaser_BETA(
  jsScanHead scan_head,
  jsLaser laser,
  jsBrightnessCorrection_BETA *correction)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetBrightnessCorrection(laser, correction);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetBrightnessCorrectionCamera_BETA(
  jsScanHead scan_head,
  jsCamera camera,
  jsBrightnessCorrection_BETA *correction)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetBrightnessCorrection(camera, correction);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetBrightnessCorrectionLaser_BETA(
  jsScanHead scan_head,
  jsLaser laser,
  jsBrightnessCorrection_BETA *correction)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetBrightnessCorrection(laser, correction);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetMinimumEncoderTravel(jsScanHead scan_head,
                                          uint32_t min_encoder_travel)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetMinimumEncoderTravel(min_encoder_travel);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetMinimumEncoderTravel(jsScanHead scan_head,
                                          uint32_t *min_encoder_travel)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *min_encoder_travel = sh->GetMinimumEncoderTravel();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED 
int32_t jsScanSystemSetIdleScanPeriod(jsScanSystem scan_system,
                                      uint32_t idle_period_us)
{
  int32_t r = 0;
  
  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->SetIdleScanPeriod(idle_period_us);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemGetIdleScanPeriod(jsScanSystem scan_system,
                                      uint32_t *idle_period_us)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    *idle_period_us = manager->GetIdleScanPeriod();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanSystemDisableIdleScanning(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_SCAN_SYSTEM;
    }

    r = manager->DisableIdleScanning();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
bool jsScanSystemIsIdleScanningEnabled(jsScanSystem scan_system)
{
  bool is_enabled = false;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return false;
    }

    is_enabled = manager->IsIdleScanningEnabled();
  } catch (std::exception &e) {
    (void)e;
    is_enabled = false;
  }

  return is_enabled;
}

EXPORTED
int32_t jsScanHeadSetIdleScanPeriod(jsScanHead scan_head,
                                    uint32_t idle_period_us)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetIdleScanPeriod(idle_period_us);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetIdleScanPeriod(jsScanHead scan_head,
                                    uint32_t *idle_period_us)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    *idle_period_us = sh->GetIdleScanPeriod();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetWindowUnconstrained(jsScanHead scan_head)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindowUnconstrained();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetWindowUnconstrainedCamera(jsScanHead scan_head,
                                              jsCamera camera)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindowUnconstrained(camera);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetWindowUnconstrainedLaser(jsScanHead scan_head,
                                             jsLaser laser)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindowUnconstrained(laser);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetWindowRectangular(jsScanHead scan_head, double window_top,
                                       double window_bottom, double window_left,
                                       double window_right)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindow(window_top,
                      window_bottom,
                      window_left,
                      window_right);
  } catch (std::range_error &e) {
    (void)e;
    r = JS_ERROR_INVALID_ARGUMENT;
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetWindowRectangularCamera(
  jsScanHead scan_head,
  jsCamera camera,
  double window_top,
  double window_bottom,
  double window_left,
  double window_right)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindow(camera,
                      window_top,
                      window_bottom,
                      window_left,
                      window_right);
  } catch (std::range_error &e) {
    (void)e;
    r = JS_ERROR_INVALID_ARGUMENT;
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetWindowRectangularLaser(
  jsScanHead scan_head,
  jsLaser laser,
  double window_top,
  double window_bottom,
  double window_left,
  double window_right)
{
  int32_t r = 0;

  try {

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetWindow(laser,
                      window_top,
                      window_bottom,
                      window_left,
                      window_right);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetPolygonWindow(
  jsScanHead scan_head,
  jsCoordinate *points,
  uint32_t points_len)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetPolygonWindow(points, points_len);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetPolygonWindowCamera(
  jsScanHead scan_head,
  jsCamera camera,
  jsCoordinate *points,
  uint32_t points_len)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetPolygonWindow(camera, points, points_len);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadSetPolygonWindowLaser(
  jsScanHead scan_head,
  jsLaser laser,
  jsCoordinate *points,
  uint32_t points_len)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->SetPolygonWindow(laser, points, points_len);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetWindowTypeCamera(
  jsScanHead scan_head,
  jsCamera camera,
  jsScanWindowType *window_type
)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowType(camera, window_type);

  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetWindowTypeLaser(
  jsScanHead scan_head,
  jsLaser laser,
  jsScanWindowType *window_type
)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowType(laser, window_type);

  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetNumberWindowPointsCamera(
  jsScanHead scan_head,
  jsCamera camera)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowCoordinatesCount(camera);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetNumberWindowPointsLaser(
  jsScanHead scan_head,
  jsLaser laser)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowCoordinatesCount(laser);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetWindowCamera(
  jsScanHead scan_head,
  jsCamera camera,
  jsCoordinate *points)
{
  int32_t r = 0;

  try {
    if (nullptr == points) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowCoordinates(camera, points);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED int32_t jsScanHeadGetWindowLaser(
  jsScanHead scan_head,
  jsLaser laser,
  jsCoordinate *points)
{
  int32_t r = 0;

  try {
    if (nullptr == points) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetWindowCoordinates(laser, points);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetStatus(jsScanHead scan_head, jsScanHeadStatus *status)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    StatusMessage msg;
    r = sh->GetStatusMessage(&msg);
    if (0 != r) {
      return r;
    }

    memcpy(status, &msg.user, sizeof(jsScanHeadStatus));
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
bool jsScanHeadIsConnected(jsScanHead scan_head)
{
  bool is_connected = false;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return false;
    }

    is_connected = sh->IsConnected();
  } catch (std::exception &e) {
    (void)e;
    is_connected = false;
  }

  return is_connected;
}

EXPORTED
int32_t jsScanHeadGetProfilesAvailable(jsScanHead scan_head)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    uint32_t count = sh->AvailableProfiles();
    r = static_cast<int32_t>(count);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadWaitUntilProfilesAvailable(jsScanHead scan_head,
                                             uint32_t count,
                                             uint32_t timeout_us)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->WaitUntilAvailableProfiles(count, timeout_us);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadClearProfiles(jsScanHead scan_head)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->ClearProfiles();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetRawProfiles(jsScanHead scan_head, jsRawProfile *profiles,
                                 uint32_t max_profiles)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetProfiles(profiles, max_profiles);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetProfiles(jsScanHead scan_head, jsProfile *profiles,
                              uint32_t max_profiles)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetProfiles(profiles, max_profiles);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetDiagnosticProfileCamera(jsScanHead scan_head,
                                             jsCamera camera,
                                             jsDiagnosticMode mode,
                                             uint32_t laser_on_time_us,
                                             uint32_t camera_exposure_time_us,
                                             jsRawProfile *profile)
{
  int32_t r = JS_ERROR_INTERNAL;

  try {
    if (nullptr == profile) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    r = sh->GetProfile(camera, 
                       mode,
                       camera_exposure_time_us,
                       laser_on_time_us,
                       profile);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetDiagnosticProfileLaser(jsScanHead scan_head,
                                            jsLaser laser,
                                            jsDiagnosticMode mode,
                                            uint32_t laser_on_time_us,
                                            uint32_t camera_exposure_time_us,
                                            jsRawProfile *profile)
{
  int32_t r = JS_ERROR_INTERNAL;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetProfile(laser, mode, camera_exposure_time_us, laser_on_time_us,
                       profile);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetDiagnosticImageCamera(jsScanHead scan_head,
                                           jsCamera camera,
                                           jsDiagnosticMode mode,
                                           uint32_t laser_on_time_us,
                                           uint32_t camera_exposure_time_us,
                                           jsCameraImage *image)
{
  int32_t r = JS_ERROR_INTERNAL;

  try {
    if (nullptr == image) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetImage(camera, camera_exposure_time_us, laser_on_time_us, image);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetDiagnosticImageLaser(jsScanHead scan_head,
                                          jsLaser laser,
                                          jsDiagnosticMode mode,
                                          uint32_t laser_on_time_us,
                                          uint32_t camera_exposure_time_us,
                                          jsCameraImage *image)
{
  int32_t r = JS_ERROR_INTERNAL;

  try {
    if (nullptr == image) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetImage(laser, camera_exposure_time_us, laser_on_time_us, image);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetDiagnosticImage(jsScanHead scan_head, jsCamera camera,
                                     jsLaser laser, jsDiagnosticMode mode,
                                     uint32_t laser_on_time_us,
                                     uint32_t camera_exposure_time_us,
                                     jsCameraImage *image)
{
  int32_t r = JS_ERROR_INTERNAL;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_SCAN_HEAD;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetImage(camera, laser, camera_exposure_time_us, laser_on_time_us,
                     image);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}
