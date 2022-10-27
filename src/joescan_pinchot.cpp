/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "joescan_pinchot.h"
#include "NetworkInterface.hpp"
#include "ScanHead.hpp"
#include "ScanManager.hpp"
#include "Version.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <cassert>
#include <cstring>
#include <cmath>

#define INVALID_DOUBLE(d) (std::isinf((d)) || std::isnan((d)))

using namespace joescan;

static std::map<uint32_t, ScanManager*> _uid_to_scan_manager;
static int _network_init_count = 0;

static unsigned int _data_format_to_stride(jsDataFormat fmt)
{
  unsigned int stride = 0;

  switch (fmt) {
    case JS_DATA_FORMAT_INVALID:
      stride = 0;
      break;
    case JS_DATA_FORMAT_XY_BRIGHTNESS_FULL:
    case JS_DATA_FORMAT_XY_FULL:
      stride = 1;
      break;
    case JS_DATA_FORMAT_XY_BRIGHTNESS_HALF:
    case JS_DATA_FORMAT_XY_HALF:
      stride = 2;
      break;
    case JS_DATA_FORMAT_XY_BRIGHTNESS_QUARTER:
    case JS_DATA_FORMAT_XY_QUARTER:
      stride = 4;
      break;
  }

  return stride;
}

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
      case (JS_ERROR_UNKNOWN):
      default:
        *error_str = "unknown error";
    }
  }
}

EXPORTED
jsScanSystem jsScanSystemCreate(jsUnits units)
{
  jsScanSystem scan_system;

  if (JS_UNITS_INCHES != units && JS_UNITS_MILLIMETER != units) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  try {
    if (0 == _network_init_count) {
      // We need to explicitly initialze the network interface first thing.
      // This is crucial for Windows since it has some extra start up code that
      // should always be done first thing in the application to ensure that
      // networking works.
      // TODO: this could probably be moved...
      NetworkInterface::InitSystem();
      _network_init_count++;
    }

    ScanManager *manager = new ScanManager(units);
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

  try {
    if (0 != _network_init_count) {
      NetworkInterface::FreeSystem();
      _network_init_count--;
    }
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = manager->ScanHeadsDiscovered(results, max_results);
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    manager->Disconnect();
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    phase_table->Reset();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return 0;
}

EXPORTED
int32_t jsScanSystemPhaseCreate(jsScanSystem scan_system)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    phase_table->CreatePhase();
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    r = phase_table->AddToLastPhaseEntry(sh, camera);
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    r = phase_table->AddToLastPhaseEntry(sh, laser);
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    r = phase_table->AddToLastPhaseEntry(sh, camera, cfg);
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    PhaseTable *phase_table = manager->GetPhaseTable();

    if (true == manager->IsScanning()) {
      return JS_ERROR_SCANNING;
    }

    r = phase_table->AddToLastPhaseEntry(sh, laser, cfg);
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
  return JS_ERROR_INVALID_ARGUMENT;
}

EXPORTED
int32_t jsScanSystemPhaseInsertLaserConfiguration(jsScanSystem scan_system,
                                                  jsScanHead scan_head,
                                                  jsLaser laser,
                                                  jsScanHeadConfiguration cfg)
{
  return JS_ERROR_INVALID_ARGUMENT;
}

EXPORTED
int32_t jsScanSystemGetMinScanPeriod(jsScanSystem scan_system)
{
  uint32_t period_us = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    period_us = manager->GetMinScanPeriod();
  } catch (std::exception &e) {
    (void)e;
    return JS_ERROR_INTERNAL;
  }

  return (int32_t)period_us;
}

EXPORTED
int32_t jsScanSystemStartScanning(jsScanSystem scan_system, uint32_t period_us,
                                  jsDataFormat fmt)
{
  int32_t r = 0;

  try {
    ScanManager *manager = _get_scan_manager_object(scan_system);
    if (nullptr == manager) {
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = manager->StopScanning();
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    auto version = sh->GetFirmwareVersion();
    *major = std::get<0>(version);
    *minor = std::get<1>(version);
    *patch = std::get<2>(version);
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (INVALID_DOUBLE(roll_degrees) || INVALID_DOUBLE(shift_x) ||
        INVALID_DOUBLE(shift_y)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetAlignment(roll_degrees, shift_x, shift_y);
    if ((0 == r) && sh->IsConnected()) {
      // changing alignment changes the window
      r = sh->SendWindow();
    }
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
    if (INVALID_DOUBLE(roll_degrees) || INVALID_DOUBLE(shift_x) ||
        INVALID_DOUBLE(shift_y)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetAlignment(camera, roll_degrees, shift_x, shift_y);
    if ((0 == r) && sh->IsConnected()) {
      // changing alignment changes the window
      r = sh->SendWindow(camera);
    }
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
    if ((nullptr == roll_degrees) || (nullptr == shift_x) ||
        (nullptr == shift_y)) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    AlignmentParams alignment;
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
    if (INVALID_DOUBLE(roll_degrees) || INVALID_DOUBLE(shift_x) ||
               INVALID_DOUBLE(shift_y)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetAlignment(laser, roll_degrees, shift_x, shift_y);
    if ((0 == r) && sh->IsConnected()) {
      // changing alignment changes the window
      r = sh->SendWindow(sh->GetPairedCamera(laser));
    }
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadGetAlignmentLaser(jsScanHead scan_head, jsLaser laser,
                                    double *roll_degrees, double *shift_x,
                                    double *shift_y, bool *is_cable_downstream)
{
  int32_t r = 0;

  try {
    if ((nullptr == roll_degrees) || (nullptr == shift_x) ||
        (nullptr == shift_y) || (nullptr == is_cable_downstream)) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    AlignmentParams alignment;
    r = sh->GetAlignment(laser, roll_degrees, shift_x, shift_y);
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
    if (INVALID_DOUBLE(window_top) || INVALID_DOUBLE(window_bottom) ||
               INVALID_DOUBLE(window_left) || INVALID_DOUBLE(window_right)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanWindow window(window_top, window_bottom, window_left, window_right);
    r = sh->SetWindow(window);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendWindow();
    }
  } catch (std::range_error &e) {
    (void)e;
    r = JS_ERROR_INVALID_ARGUMENT;
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
    if (nullptr == status) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanManager &manager = sh->GetScanManager();
    StatusMessage msg;

    if (false == manager.IsConnected()) {
      return JS_ERROR_NOT_CONNECTED;
    } else if (0 != sh->GetStatusMessage(&msg)) {
      return JS_ERROR_INTERNAL;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (JS_SCAN_HEAD_PROFILES_MAX < count) {
      count = JS_SCAN_HEAD_PROFILES_MAX;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    sh->ClearProfiles();
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
    if (nullptr == profiles) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    auto p = sh->GetProfiles(static_cast<int>(max_profiles));
    uint32_t total = (max_profiles < static_cast<uint32_t>(p.size()))
                       ? max_profiles
                       : static_cast<uint32_t>(p.size());
    for (uint32_t n = 0; n < total; n++) {
      // `ScanHead` returns vector of shared pointers; dereference to copy
      profiles[n] = *p[n];
    }

    // return number of profiles copied
    r = static_cast<int32_t>(total);
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
    if (nullptr == profiles) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    auto p = sh->GetProfiles(static_cast<int>(max_profiles));
    uint32_t total = (max_profiles < static_cast<uint32_t>(p.size()))
                       ? max_profiles
                       : static_cast<uint32_t>(p.size());

    for (uint32_t m = 0; m < total; m++) {
      profiles[m].scan_head_id = p[m]->scan_head_id;
      profiles[m].camera = p[m]->camera;
      profiles[m].laser = p[m]->laser;
      profiles[m].timestamp_ns = p[m]->timestamp_ns;
      profiles[m].flags = p[m]->flags;
      profiles[m].sequence_number = p[m]->sequence_number;
      profiles[m].laser_on_time_us = p[m]->laser_on_time_us;
      profiles[m].format = p[m]->format;
      profiles[m].packets_received = p[m]->packets_received;
      profiles[m].packets_expected = p[m]->packets_expected;
      profiles[m].num_encoder_values = p[m]->num_encoder_values;
      memcpy(profiles[m].encoder_values, p[m]->encoder_values,
             p[m]->num_encoder_values * sizeof(uint64_t));

      unsigned int stride = _data_format_to_stride(profiles[m].format);
      unsigned int len = 0;
      for (unsigned int n = 0; n < p[m]->data_len; n += stride) {
        if ((JS_PROFILE_DATA_INVALID_XY != p[m]->data[n].x) ||
            (JS_PROFILE_DATA_INVALID_XY != p[m]->data[n].y)) {
          // Note: Only need to check X/Y since we only support data types with
          // X/Y coordinates alone or X/Y coordinates with brightness.
          profiles[m].data[len++] = p[m]->data[n];
        }
      }
      profiles[m].data_len = len;
    }
    // return number of profiles copied
    r = static_cast<int32_t>(total);
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetProfile(camera, camera_exposure_time_us, laser_on_time_us,
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
    if (nullptr == profile) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    if (JS_DIAGNOSTIC_FIXED_EXPOSURE != mode) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->GetProfile(laser, camera_exposure_time_us, laser_on_time_us,
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (nullptr == image) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
