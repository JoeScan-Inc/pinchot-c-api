/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_SCAN_MANAGER_H
#define JOESCAN_SCAN_MANAGER_H

#include "AlignmentParams.hpp"
#include "PhaseTable.hpp"
#include "ScanSyncManager.hpp"
#include "Version.hpp"
#include "joescan_pinchot.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace joescan {
class ScanHead;

class ScanManager {
 public:
  /**
   * @brief Creates a new scan manager object;
   */
  ScanManager(jsUnits units, ScanSyncManager *scansync);

  /**
   * @brief Destructor for the `ScanManager` object.
   */
  ~ScanManager();

  /**
   * @brief Returns the unique identifier for the scan system.
   *
   * @returns The UID of the scan system.
   */
  uint32_t GetUID() const;

  /**
   * @brief Performs broadcast discover on all available network interfaces.
   *
   * @return The total number of scan heads discovered on success, negative
   * value mapping to `jsError` on error.
   */
  int32_t Discover();

  /**
   * @brief Obtains information on the scan heads discovered during the call to
   * the `Discover` function.
   *
   * @param results Pointer to array to be filled with discover data.
   * @param max_results Length of `results` array.
   * @return The total number of scan heads discovered on success, negative
   * value mapping to `jsError` on error.
   */
  int32_t ScanHeadsDiscovered(jsDiscovered *results, uint32_t max_results);

  int32_t DiscoverScanSyncs(jsScanSyncDiscovered *discovered,
                            uint32_t max_results);

  int32_t SetScanSyncEncoder(uint32_t serial_main, uint32_t serial_aux1,
                             uint32_t serial_aux2);
  int32_t GetScanSyncEncoder(uint32_t *serial_main, uint32_t *serial_aux1,
                             uint32_t *serial_aux2);
  /**
   * @brief Creates a `ScanHead` object used to receive scan data.
   *
   * @param serial_number The serial number of the scan head.
   * @param type Product type of the scan head.
   * @param id The ID to associate with the scan head.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t CreateScanHead(uint32_t serial_number, uint32_t id);

  /**
   * @brief Gets a `ScanHead` object used to receive scan data.
   *
   * @param serial_number The serial number of the scan head to get.
   * @param A shared pointer to an object representing the scan head.
   */
  ScanHead* GetScanHeadBySerial(uint32_t serial_number);

  /**
   * @brief Gets a `ScanHead` object used to receive scan data.
   *
   * @param id The ID of the scan head to get.
   * @param A shared pointer to an object representing the scan head.
   */
  ScanHead* GetScanHeadById(uint32_t id);

  /**
   * @brief Removes a `ScanHead` object from use.
   *
   * @param serial_number The serial number of the scan head to remove.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t RemoveScanHead(uint32_t serial_number);

  /**
   * @brief Removes a `ScanHead` object from use.
   *
   * @param scan_head A pointer of the scan head object to remove.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t RemoveScanHead(ScanHead* scan_head);

  /**
   * @brief Removes all created `ScanHead` objects from use.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t RemoveAllScanHeads();

  /**
   * @brief Returns the total number of `ScanHead` objects associated with
   * the `ScanManager`.
   *
   * @return Total number of `ScanHeads`.
   */
  uint32_t GetNumberScanners() const;

  int32_t PhaseClearAll();
  int32_t PhaseCreate();
  int32_t PhaseInsert(ScanHead *scan_head, jsCamera camera);
  int32_t PhaseInsert(ScanHead *scan_head, jsLaser laser);
  int32_t PhaseInsert(ScanHead *scan_head, jsCamera camera,
                      jsScanHeadConfiguration *config);
  int32_t PhaseInsert(ScanHead *scan_head, jsLaser laser,
                      jsScanHeadConfiguration *config);

  /**
   * @brief Enables and sets the idle scan period for the scan system.
   * 
   * @param period_us The idle scan period in microseconds.
   * @return `0` on success, negative value mapping to `jsError` on error.
  */
  int32_t SetIdleScanPeriod(uint32_t period_us);

  /**
   * @brief Disables idle scanning for the scan system.
   * 
   * @return `0` on success, negative value mapping to `jsError` on error.
  */
  int32_t DisableIdleScanning();

  /**
   * @brief Gets the idle scan period for the scan system.
   *
   * @return The idle scan period in microseconds for the system.
   */
  uint32_t GetIdleScanPeriod();

  /**
   * @brief Gets the idle scan enabled state for the scan system.
   * 
   * @return The idle scan enabled state for the system.
  */
  bool IsIdleScanningEnabled();

  /**
   * @brief Attempts to connect to all `ScanHead` objects that were previously
   * created using `CreateScanHead` call.
   *
   * @param timeout_s Maximum time to allow for connection.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t Connect(uint32_t timeout_s);

  /**
   * @brief Disconnects all `ScanHead` objects that were previously connected
   * from calling `Connect`.
   */
  int32_t Disconnect();

  /**
   * @brief Starts scanning on all `ScanHead` objects that were connected
   * using the `Connect` function.
   *
   * @param period_us Scan period in microseconds.
   * @param fmt The data format of scan data.
   * @param is_frame_scanning Set to `true` to enable frame scanning
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int StartScanning(uint32_t period_us, jsDataFormat fmt,
                    bool is_frame_scanning=false);

  uint32_t GetProfilesPerFrame() const;
  int32_t WaitUntilFrameAvailable(uint32_t timeout_us);
  int32_t GetFrame(jsProfile *profiles);
  int32_t GetFrame(jsRawProfile *profiles);
  int32_t ClearFrames();

  /**
   * @brief Stop scanning on all `ScanHead` objects that were told to scan
   * through the `StartScanning` function.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t StopScanning();

  /**
   * @brief Sends configuration data to all the scan heads.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t Configure();

  /**
   * @brief Gets the minimum scan period achievable for a given scan system.
   *
   * @return The minimum scan period in microseconds.
   */
  uint32_t GetMinScanPeriod();

  /**
   * @brief Gets the measurement units specified for the `ScanManager`.
   *
   * @return The configured measurement units.
   */
  jsUnits GetUnits() const;

  /**
   * @brief Boolean state function used to determine if the `ScanManager` has
   * connected to the `ScanHead` objects.
   *
   * @return Boolean `true` if connected, `false` if disconnected.
   */
  inline bool IsConnected() const;

  /**
   * @brief Boolean state function used to determine if the `ScanManager` and
   * `ScanHead` objects are actively scanning.
   *
   * @return Boolean `true if scanning, `false` otherwise.
   */
  inline bool IsScanning() const;

  bool IsConfigured() const;

  std::string GetErrorExtended() const;

 private:
  enum SystemState { Disconnected, Connected, Scanning, Close };

  int32_t BroadcastDiscover(
    std::map<uint32_t, std::shared_ptr<jsDiscovered>> &discovered);
  void KeepAliveThread();
  void HeartBeatThread();

  // If more profiles queued than threshold, assume partial frame ready to read
  const uint32_t kFrameSizeThreshold = 50;

  std::map<uint32_t, std::shared_ptr<jsDiscovered>> m_serial_to_discovered;
  std::map<uint32_t, ScanHead*> m_serial_to_scan_head;
  std::map<uint32_t, ScanHead*> m_id_to_scan_head;
  std::map<jsEncoder, uint32_t> m_encoder_to_serial;
  std::string m_error_extended_str;
  std::thread m_keep_alive_thread;
  std::thread m_heart_beat_thread;
  std::condition_variable m_condition;
  std::mutex m_mutex;

  ScanSyncManager *m_scansync;
  PhaseTable m_phase_table;
  SystemState m_state;
  jsUnits m_units;

  SemanticVersion m_version_scan_head_lowest;
  SemanticVersion m_version_scan_head_highest;

  static uint32_t m_uid_count;
  uint32_t m_uid;
  uint32_t m_min_scan_period_us;
  uint32_t m_scan_period_us;
  uint32_t m_idle_scan_period_us;
  uint32_t m_frame_current_sequence;
  bool m_is_frame_scanning;
  bool m_is_frame_ready;
  bool m_is_user_encoder_map;
  bool m_is_encoder_dirty;
  bool m_is_idle_scan_enabled;
};

inline bool ScanManager::IsConnected() const
{
  return (m_state == SystemState::Connected) ||
         (m_state == SystemState::Scanning);
}

inline bool ScanManager::IsScanning() const
{
  return m_state == SystemState::Scanning;
}
} // namespace joescan

#endif // JOESCAN_SCAN_MANAGER_H
