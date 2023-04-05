/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "joescan_pinchot.h"
#include "BroadcastDiscover.hpp"
#include "NetworkInterface.hpp"
#include "ScanHead.hpp"
#include "ScanManager.hpp"
#include "Version.hpp"

#include "MessageUpdateClient_generated.h"
#include "MessageUpdateServer_generated.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <cassert>
#include <cstring>
#include <cmath>

using namespace joescan;

#define INVALID_DOUBLE(d) (std::isinf((d)) || std::isnan((d)))

static std::map<uint32_t, ScanManager*> _uid_to_scan_manager;
static int _network_init_count = 0;

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
      case (JS_ERROR_UNKNOWN):
      default:
        *error_str = "unknown error";
    }
  }
}

EXPORTED
int32_t jsPowerCycleScanHead(uint32_t serial_number)
{
  constexpr uint16_t kUpdatePort = 21232;
  uint32_t ip_addr = 0;

  try {
    // First check if the head can be discovered.
    std::map<uint32_t, std::shared_ptr<jsDiscovered>> serial_to_discovered;
    int r = BroadcastDiscover(serial_to_discovered);
    if (serial_to_discovered.find(serial_number) ==
        serial_to_discovered.end()) {
      // Failed to find in BroadcastDiscover, try again using MDNS
      r = NetworkInterface::ResolveIpAddressMDNS(serial_number, &ip_addr);
      if (0 != r) {
        return JS_ERROR_NOT_DISCOVERED;
      }
    } else {
      auto discovered = serial_to_discovered[serial_number];
      ip_addr = discovered->ip_addr;
    }

    auto iface = NetworkInterface::InitTCPSocket(ip_addr, kUpdatePort, 10);

    using namespace joescan::schema::update::client;

    flatbuffers::FlatBufferBuilder builder(0x20);

    builder.Clear();
    auto msg_offset = CreateMessageClient(builder, MessageType_REBOOT_REQUEST,
                                          MessageData_NONE);
    builder.Finish(msg_offset);

    char *msg = reinterpret_cast<char *>(builder.GetBufferPointer());
    uint32_t msg_len = builder.GetSize();

    // NOTE: sending little-endian as to keep with approach used by Flatbuffers
    r = send(iface.sockfd,
             reinterpret_cast<char *>(&msg_len),
             sizeof(uint32_t), 0);
    if (sizeof(uint32_t) != r) {
      return JS_ERROR_INTERNAL;
    }

    r = send(iface.sockfd, msg, msg_len, 0);
    if (r != static_cast<int>(builder.GetSize())) {
      return JS_ERROR_INTERNAL;
    }
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
    if (nullptr == mask) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetExclusionMask(camera, mask);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendExclusionMask(camera);
    }
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
    if (nullptr == mask) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetExclusionMask(laser, mask);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendExclusionMask(laser);
    }
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
    if (nullptr == mask) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (nullptr == mask) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (nullptr == correction) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetBrightnessCorrection(camera, correction);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendBrightnessCorrection(camera);
    }
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
    if (nullptr == correction) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetBrightnessCorrection(laser, correction);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendBrightnessCorrection(laser);
    }
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
    if (nullptr == correction) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
    if (nullptr == correction) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    *min_encoder_travel = sh->GetMinimumEncoderTravel();
  } catch (std::exception &e) {
    (void)e;
    r = JS_ERROR_INTERNAL;
  }

  return r;
}

EXPORTED
int32_t jsScanHeadSetIdleScanPeriod(jsScanHead scan_head,
                                    uint32_t idle_period_us)
{
  int32_t r = 0;

  try {
    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
      return JS_ERROR_INVALID_ARGUMENT;
    }

    *idle_period_us = sh->GetIdleScanPeriod();
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
    if (INVALID_DOUBLE(window_top) || INVALID_DOUBLE(window_bottom) ||
        INVALID_DOUBLE(window_left) || INVALID_DOUBLE(window_right)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanWindow window(window_top, window_bottom, window_left, window_right);
    r = sh->SetWindow(camera, window);
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
    if (INVALID_DOUBLE(window_top) || INVALID_DOUBLE(window_bottom) ||
        INVALID_DOUBLE(window_left) || INVALID_DOUBLE(window_right)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    ScanWindow window(window_top, window_bottom, window_left, window_right);
    r = sh->SetWindow(laser, window);
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

EXPORTED int32_t jsScanHeadSetPolygonWindow(
  jsScanHead scan_head,
  jsCoordinate *points,
  uint32_t points_len)
{
  int32_t r = 0;

  try {
    if (nullptr == points) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetPolygonWindow(points, points_len);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendWindow();
    }
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
    if (nullptr == points) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetPolygonWindow(camera, points, points_len);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendWindow();
    }
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
    if (nullptr == points) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
    }

    r = sh->SetPolygonWindow(laser, points, points_len);
    if ((0 == r) && sh->IsConnected()) {
      r = sh->SendWindow();
    }
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
    if (nullptr == profiles) {
      return JS_ERROR_NULL_ARGUMENT;
    }

    ScanHead *sh = _get_scan_head_object(scan_head);
    if (nullptr == sh) {
      return JS_ERROR_INVALID_ARGUMENT;
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
