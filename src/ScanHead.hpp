/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_SCAN_HEAD_H
#define JOESCAN_SCAN_HEAD_H

#include "DataPacket.hpp"
#include "DynamicData.hpp"
#include "ProfileQueue.hpp"
#include "TCPSocket.hpp"
#include "ScanManager.hpp"
#include "ScanHeadModel.hpp"
#include "ScanWindow.hpp"
#include "StatusMessage.hpp"
#include "Version.hpp"
#include "joescan_pinchot.h"

#include "readerwritercircularbuffer.h"
#include "flatbuffers/flatbuffers.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <shared_mutex>

namespace joescan {

// forward declaration
class CameraLaserIterator;

class ScanHead {
 public:
  /**
   * Initializes a `ScanHead` object.
   *
   * @param manager Reference to scan manager.
   * @param type The product type of the scan head.
   * @param serial_number The serial number of the scan head to create.
   * @param id The unique identifier to associate with the scan head.
   */
  ScanHead(ScanManager &manager, jsDiscovered &discovered, uint32_t id);

  /**
   * Destroys `ScanHead` object.
   */
  ~ScanHead();

  /**
   * Gets the scan head product type.
   *
   * @return Enum value representing scan head type.
   */
  jsScanHeadType GetType() const;

  /**
   * Gets the serial number of the scan head.
   *
   * @return String representation of the serial number.
   */
  uint32_t GetSerialNumber() const;

  /**
   * Gets the ID of the scan head.
   *
   * @return Numeric ID of the scan head.
   */
  uint32_t GetId() const;

  /** Gets the binary representation of the IP address of the scan head.
   * Note, this can be converted to a string representation by using the
   * `inet_ntop` function.
   *
   * @return The binary representation of the scan head's IP address.
   */
  uint32_t GetIpAddress() const;

  SemanticVersion GetFirmwareVersion() const;

  jsScanHeadCapabilities GetCapabilities() const;

  /**
   * Performs client request to scan head to connect.
   *
   * @param timeout_s Connection timeout in seconds.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int Connect(uint32_t timeout_s);

  /**
   * Verifies that the client connect request performed by `Connect` succeeded
   * and that the client is compatible with the server.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int ConnectVerify();

  /**
   * Performs client request to scan head to disconnect.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int Disconnect();

  /**
   * Performs client request to scan head to configure scan parameters.
   *
   * @param period_us The scan period in microseconds
   * @param fmt The format of scan data to receive
   * @param is_frame_scanning Set to true if frame scanning
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SendScanConfiguration(uint32_t period_us, jsDataFormat fmt,
                            bool is_frame_scanning);

  /**
   * Performs client request to scan head to configure alignment values
   * 
   * @return `0` on success, negative value mapping to `jsError` on error.
   *
  */
 int SendScanAlignmentValue();

  /**
   * Sends Keep Alive message to the scan head.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SendKeepAlive();
  /**
   * Sends HeartBeat request, listen for beat from server.
   * @param timeout defines how long to wait on reading from server
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int GetHeartBeat(struct timeval *timeout);

  int SendEncoders(uint32_t serial_main, uint32_t serial_aux1,
                   uint32_t serial_aux2);

  /**
   * Performs client request to the scan head to start scanning.
   *
   * @param start_time_ns The global time to have the scan head start scanning
   *        Empty argument will leave the scan head to determine it's own time
   *        to start.
   * @param is_frame_scanning Set to true if frame scanning
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int StartScanning(uint64_t start_time_ns, bool is_frame_scanning);

  /**
   * Performs client request to the scan head to stop scanning.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int StopScanning();

  int SendBrightnessCorrection();
  int SendExclusionMask();
  int SendWindow();

  /**
   * Returns boolean confirming connection of the client to the scan head.
   *
   * @return True if connected, false is disconnected.
   */
  bool IsConnected() const;

  bool IsScanning() const;

  int32_t GetImage(jsCamera camera, jsLaser laser, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);
  int32_t GetImage(jsCamera camera, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);

  int32_t GetImage(jsLaser laser, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);

  int32_t GetProfile(jsCamera camera, jsDiagnosticMode mode,
                     uint32_t camera_exposure_us, uint32_t laser_on_time_us,
                     jsRawProfile *profile);

  int32_t GetProfile(jsLaser laser, jsDiagnosticMode mode,
                     uint32_t camera_exposure_us, uint32_t laser_on_time_us,
                     jsRawProfile *profile);
  int32_t GetProfile(jsCamera camera, jsLaser laser, jsDiagnosticMode mode,
                     uint32_t camera_exposure_us, uint32_t laser_on_time_us,
                     jsRawProfile *profile);

  /**
   * Returns the number of profiles that are available to be read from calling
   * the `GetProfiles` function.
   *
   * @return The number of profiles able to be read.
   */
  uint32_t AvailableProfiles();

  /**
   * Blocks until the number of profiles requested are available to be read.
   *
   * @param count The desired number of profiles to wait for.
   * @param timeout_us The max time to wait for in microseconds.
   * @return The number of profiles able to be read.
   */
  uint32_t WaitUntilAvailableProfiles(uint32_t count, uint32_t timeout_us);

  int32_t ClearProfiles();
  int32_t GetProfiles(jsRawProfile *profiles, uint32_t max_profiles);
  int32_t GetProfiles(jsProfile *profiles, uint32_t max_profiles);

  /**
   * Requests a new status message from the scan head.
   *
   * @param msg The new status message.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int GetStatusMessage(StatusMessage *msg);

  /**
   * Requests a new scansync message from the scan head.
   * @param scan_syncs ScanSyncs seen by scanner
   * @param max_results number of ScanSyncs, limited by JS_ENCODER_MAX
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int32_t SendScanSyncStatusRequest(
    jsScanSyncDiscovered *scan_syncs, uint32_t max_results);

  /**
   * Obtains the last requested status message from a scan head.
   *
   * @return The last requested status message.
   */
  StatusMessage GetLastStatusMessage();

  /**
   * Clears out the last reported status message from a scan head.
   */
  void ClearStatusMessage();

  /**
   * Gets the scan manager that owns this scan head.
   *
   * @return Reference to `ScanManager` object.
   */
  ScanManager &GetScanManager();

  ProfileQueue *GetProfileQueue();

  /**
   * Verifies a given `jsScanHeadConfiguration` to ensure it is valid and can
   * be applied to the given `ScanHead`.
   *
   * @return Boolean `true` if valid, `false` if not.
   */
  bool IsConfigurationValid(jsScanHeadConfiguration &cfg);

  /**
   * Configures a scan head according to the specified parameters.
   *
   * @param config A reference to the configuration parameters.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SetConfiguration(jsScanHeadConfiguration &cfg);

  /**
   * Gets the current configuration of the scan head.
   *
   * @return The configuration of the scan head.
   */
  jsScanHeadConfiguration GetConfiguration() const;

  /**
   * Gets the current configuration of the scan head.
   *
   * @return The configuration of the scan head.
   */
  jsScanHeadConfiguration GetConfigurationDefault() const;

  /**
   * Gets the minimum period in microseconds that the `ScanHead` can be
   * commanded to scan at.
   *
   * @return The period in microseconds.
   */
  uint32_t GetMinScanPeriod() const;

  /**
   * Clears all camera / laser pairs configured for scanning.
   */
  void ResetScanPairs();

  jsCamera GetPairedCamera(jsLaser laser) const;
  jsLaser GetPairedLaser(jsCamera camera) const;
  uint32_t GetCameraLaserPairCount() const;

  /**
   * Adds a new camera / laser pair within the `ScanHead` to be configured for
   * scanning.
   *
   * @param camera The camera id.
   * @param laser The laser id.
   * @param cfg Reference to the configuration to be applied to the pair.
   * @param end_offset_us Time in microseconds when scan is to stop in period.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int AddScanPair(jsCamera camera, jsLaser laser, jsScanHeadConfiguration &cfg,
                  uint32_t end_offset_us);

  /**
   * Gets the maximum number of camera / laser pairs that can be configured for
   * the `ScanHead`.
   *
   * @return The total number of scan pairs supported.
   */
  uint32_t GetScanPairsMax() const;

  /**
   * Gets the total number of scan pairs set. This should be equal to the
   * number of times `AddScanPair` is successfully called.
   *
   * @return The total number of scan pairs configured.
   */
  uint32_t GetScanPairsCount() const;

  int SetCableOrientation(jsCableOrientation cable);
  jsCableOrientation GetCableOrientation() const;

  /**
   * Sets the alignment settings for the scan head.
   *
   * @param camera The camera to apply the alignment to.
   * @param alignment The alignment settings.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SetAlignment(double roll_degrees, double shift_x, double shift_y);
  int SetAlignment(jsCamera camera, double roll_degrees, double shift_x,
                   double shift_y);
  int SetAlignment(jsLaser laser, double roll_degrees, double shift_x,
                   double shift_y);
  int GetAlignment(jsCamera camera, double *roll_degrees, double *shift_x,
                   double *shift_y);
  int GetAlignment(jsLaser laser, double *roll_degrees, double *shift_x,
                   double *shift_y);

  int SetExclusionMask(jsExclusionMask *mask);
  int SetExclusionMask(jsCamera camera, jsExclusionMask *mask);
  int SetExclusionMask(jsLaser laser, jsExclusionMask *mask);
  int GetExclusionMask(jsCamera camera, jsExclusionMask *mask);
  int GetExclusionMask(jsLaser laser, jsExclusionMask *mask);

  int SetBrightnessCorrection(jsCamera camera,
                              jsBrightnessCorrection_BETA *correction);
  int SetBrightnessCorrection(jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);
  int GetBrightnessCorrection(jsCamera camera,
                              jsBrightnessCorrection_BETA *correction);
  int GetBrightnessCorrection(jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);

  uint32_t GetMinimumEncoderTravel() const;
  int32_t SetMinimumEncoderTravel(uint32_t travel);

  uint32_t GetIdleScanPeriod() const;
  int32_t SetIdleScanPeriod(uint32_t period_us);

  uint32_t GetLastSequenceNumber() const;
  /**
   * Sets the window to be used for scanning with the scan head.
   *
   * @param window The scan window.
   */
  int SetWindowUnconstrained();
  int SetWindowUnconstrained(jsCamera camera);
  int SetWindowUnconstrained(jsLaser laser);
  int SetWindow(double top, double bottom, double left, double right);
  int SetWindow(jsCamera camera,
                double top,
                double bottom,
                double left,
                double right);
  int SetWindow(jsLaser laser,
                double top,
                double bottom,
                double left,
                double right);
  int SetPolygonWindow(jsCoordinate *points, uint32_t points_len);
  int SetPolygonWindow(jsCamera camera, jsCoordinate *points,
                       uint32_t points_len);
  int SetPolygonWindow(jsLaser laser, jsCoordinate *points,
                       uint32_t points_len);
  int GetWindowType(jsCamera camera, jsScanWindowType *type);
  int GetWindowType(jsLaser laser, jsScanWindowType *type);
  int GetWindowCoordinatesCount(jsCamera camera);
  int GetWindowCoordinatesCount(jsLaser laser);
  int GetWindowCoordinates(jsCamera camera, jsCoordinate *points);
  int GetWindowCoordinates(jsLaser laser, jsCoordinate *points);

  bool IsDirty() const;
  void ClearDirty();

  std::string GetErrorExtended() const;

 protected:
  friend CameraLaserIterator;

  int SetAlignment(jsCamera camera, jsLaser laser, double roll_degrees,
                   double shift_x, double shift_y);
  int GetAlignment(jsCamera camera, jsLaser laser, double *roll_degrees,
                   double *shift_x, double *shift_y);
  int SetExclusionMask(jsCamera camera, jsLaser laser, jsExclusionMask *mask);
  int GetExclusionMask(jsCamera camera, jsLaser laser, jsExclusionMask *mask);
  int SendExclusionMask(jsCamera camera, jsLaser laser);
  int SetBrightnessCorrection(jsCamera camera,
                              jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);
  int GetBrightnessCorrection(jsCamera camera,
                              jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);
  int SendBrightnessCorrection(jsCamera camera, jsLaser laser);
  int SetWindow(jsCamera camera, jsLaser laser, ScanWindow &window);
  int SendWindow(jsCamera camera, jsLaser laser);
  int SetPolygonWindow(jsCamera camera, jsLaser laser, jsCoordinate *points,
                       uint32_t points_len);

  struct ScanPair {
    jsCamera camera;
    jsLaser laser;
    jsScanHeadConfiguration config;
    uint32_t end_offset_us;
  };

  // The JS-50 theoretical max packet size is 8k plus header, in reality the
  // max size is 1456 * 4 + header. Using 6k.
  static const int kMaxPacketSize = 61440;
  // JS-50 in image mode will have 4 rows of 1456 pixels for each packet.
  static const int kImageDataSize = 4 * 1456;
  // Port used to access REST interface
  static const uint32_t kRESTport = 8080;

  int ProcessProfile(DataPacket *packet, jsRawProfile *raw);
  void ThreadScanningReceive();

  ScanManager &m_scan_manager;
  ScanHeadModel m_model;
  StatusMessage m_status;
  ProfileQueue m_profiles;
  jsDataFormat m_format;

  std::unique_ptr<TCPSocket> m_sock_ctrl;
  std::unique_ptr<DynamicData> m_data;
  std::vector<ScanPair> m_scan_pairs;

  flatbuffers::FlatBufferBuilder m_builder;

  std::string m_error_extended_str;
  std::condition_variable m_new_data_cv;
  std::mutex m_new_data_mtx;
  std::thread m_receive_thread;
  std::mutex m_mutex;
  std::shared_mutex m_queue_mutex;

  SemanticVersion m_firmware_version;
  uint32_t m_ip_address;
  std::string m_client_name;
  uint32_t m_client_ip_address;
  uint8_t *m_packet_buf;
  uint32_t m_packet_buf_len;
  uint32_t m_scan_period_us;
  uint32_t m_min_ecoder_travel;
  uint64_t m_idle_scan_period_ns;
  int64_t m_last_encoder;
  uint64_t m_last_timestamp;
  uint32_t m_last_sequence;
  volatile bool m_is_receive_thread_active;
  bool m_is_frame_scanning;
  bool m_is_scanning;
  bool m_is_heart_beating;
};

} // namespace joescan

#endif // JOESCAN_SCAN_HEAD_H
