/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_SCAN_HEAD_H
#define JOESCAN_SCAN_HEAD_H

#include "TCPSocket.hpp"
#include "ScanManager.hpp"
#include "ScanWindow.hpp"
#include "StatusMessage.hpp"
#include "Version.hpp"
#include "joescan_pinchot.h"

#include "ScanHeadSpecification_generated.h"
#include "readerwritercircularbuffer.h"
#include "flatbuffers/flatbuffers.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace joescan {

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

  SemanticVersion GetFirmwareVersion();

  jsScanHeadCapabilities GetCapabilities();

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
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SendScanConfiguration();

  /**
   * Sends Keep Alive message to the scan head.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SendKeepAlive();

  /**
   * Performs client request to the scan head to start scanning.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int StartScanning();

  /**
   * Performs client request to the scan head to stop scanning.
   *
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int StopScanning();

  /**
   * Returns boolean confirming connection of the client to the scan head.
   *
   * @return True if connected, false is disconnected.
   */
  bool IsConnected();

  bool IsScanning();

  int32_t GetImage(jsCamera camera, jsLaser laser, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);
  int32_t GetImage(jsCamera camera, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);

  int32_t GetImage(jsLaser laser, uint32_t camera_exposure_us,
                   uint32_t laser_on_time_us, jsCameraImage *image);

  int32_t GetProfile(jsCamera camera, uint32_t camera_exposure_us,
                     uint32_t laser_on_time_us, jsRawProfile *profile);

  int32_t GetProfile(jsLaser laser, uint32_t camera_exposure_us,
                     uint32_t laser_on_time_us, jsRawProfile *profile);
  int32_t GetProfile(jsCamera camera, jsLaser laser,
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


  int32_t GetProfiles(jsRawProfile *profiles, uint32_t max_profiles);
  int32_t GetProfiles(jsProfile *profiles, uint32_t max_profiles);

  /**
   * Empties the circular buffer used to store received profiles from the
   * scan head.
   */
  void ClearProfiles();

  /**
   * Requests a new status message from the scan head.
   *
   * @param msg The new status message.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int GetStatusMessage(StatusMessage *msg);

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
   * Sets the data format for scanning of the scan head.
   *
   * @param fmt The format of scan data to request.
   * @return `0` on success, negative value mapping to `jsError` on error.
   */
  int SetDataFormat(jsDataFormat format);

  /**
   * Gets the data format for scanning of the scan head.
   *
   * @return The format of scan data requested.
   */
  jsDataFormat GetDataFormat() const;

  /**
   * Sets the period in microseconds by which new data is generated.
   *
   * @param period_us The scan period in microseconds.
   * @return Zero on success, negative value on error.
   */
  int SetScanPeriod(uint32_t period_us);

  /**
   * Gets the period in microseconds by which new data is generated.
   *
   * @return The period in microseconds.
   */
  uint32_t GetScanPeriod() const;

  /**
   * Gets the minimum period in microseconds that the `ScanHead` can be
   * commanded to scan at.
   *
   * @return The period in microseconds.
   */
  uint32_t GetMinScanPeriod();

  /**
   * Clears all camera / laser pairs configured for scanning.
   */
  void ResetScanPairs();

  jsCamera GetPairedCamera(jsLaser laser);
  jsLaser GetPairedLaser(jsCamera camera);

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
  int AddScanPair(jsCamera camera, jsLaser laser,
                     jsScanHeadConfiguration &cfg, uint32_t end_offset_us);

  /**
   * Gets the maximum number of camera / laser pairs that can be configured for
   * the `ScanHead`.
   *
   * @return The total number of scan pairs supported.
   */
  uint32_t GetMaxScanPairs();

  int SetCableOrientation(jsCableOrientation cable);
  jsCableOrientation GetCableOrientation();

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
  int SendExclusionMask();
  int SendExclusionMask(jsCamera camera);
  int SendExclusionMask(jsLaser laser);

  int SetBrightnessCorrection(jsCamera camera,
                              jsBrightnessCorrection_BETA *correction);
  int SetBrightnessCorrection(jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);
  int GetBrightnessCorrection(jsCamera camera,
                              jsBrightnessCorrection_BETA *correction);
  int GetBrightnessCorrection(jsLaser laser,
                              jsBrightnessCorrection_BETA *correction);
  int SendBrightnessCorrection();
  int SendBrightnessCorrection(jsCamera camera);
  int SendBrightnessCorrection(jsLaser laser);

  uint32_t GetMinimumEncoderTravel();
  int32_t SetMinimumEncoderTravel(uint32_t travel);

  uint32_t GetIdleScanPeriod();
  int32_t SetIdleScanPeriod(uint32_t period_us);

  /**
   * Sets the window to be used for scanning with the scan head.
   *
   * @param window The scan window.
   */
  int SetWindow(ScanWindow &window);
  int SetWindow(jsCamera camera, ScanWindow &window);
  int SetWindow(jsLaser laser, ScanWindow &window);
  int SendWindow();
  int SendWindow(jsCamera camera);
  int SendWindow(jsLaser laser);

  int SetPolygonWindow(jsCoordinate *points, uint32_t points_len);
  int SetPolygonWindow(jsCamera camera, jsCoordinate *points,
                       uint32_t points_len);
  int SetPolygonWindow(jsLaser laser, jsCoordinate *points,
                       uint32_t points_len);

  int GetWindow(jsCamera camera, ScanWindow *window);
  int GetWindow(jsLaser laser, ScanWindow *window);

 private:
  bool IsPairValid(jsCamera camera, jsLaser laser);
  bool IsCameraValid(jsCamera camera);
  bool IsLaserValid(jsLaser laser);

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
  int GetWindow(jsCamera camera, jsLaser laser, ScanWindow *window);

  typedef joescan::schema::client::ScanHeadSpecificationT ScanHeadSpec;

  struct ScanPair {
    jsCamera camera;
    jsLaser laser;
    jsScanHeadConfiguration config;
    uint32_t end_offset_us;
  };

  static const int kMaxCircularBufferSize = JS_SCAN_HEAD_PROFILES_MAX;
  // The JS-50 theoretical max packet size is 8k plus header, in reality the
  // max size is 1456 * 4 + header. Using 6k.
  static const int kMaxPacketSize = 61440;
  // JS-50 in image mode will have 4 rows of 1456 pixels for each packet.
  static const int kImageDataSize = 4 * 1456;
  // Port used to access REST interface
  static const uint32_t kRESTport = 8080;

  static const uint32_t kMaxAverageIntensity = 255;
  static const uint32_t kMaxSaturationPercentage = 100;
  static const uint32_t kMaxSaturationThreshold = 1023;
  static const uint32_t kMaxLaserDetectionThreshold = 1023;

  void LoadScanHeadSpecification(jsScanHeadType type, ScanHeadSpec *spec);
  uint32_t CameraLaserIdxBegin();
  uint32_t CameraLaserIdxEnd();
  std::pair<jsCamera, jsLaser> CameraLaserNext(uint32_t n);

  int ProcessProfile(uint8_t *buf, uint32_t len, jsRawProfile *raw);
  void ReceiveMain();

  int TCPSend(flatbuffers::FlatBufferBuilder &builder);
  int TCPRead(uint8_t *buf, uint32_t len, SOCKET fd);
  int TCPRead(uint8_t *buf, uint32_t len, uint32_t *size, SOCKET fd);
  int32_t CameraIdToPort(jsCamera camera);
  jsCamera CameraPortToId(uint32_t port);
  int32_t LaserIdToPort(jsLaser laser);
  jsLaser LaserPortToId(uint32_t port);

  ScanManager &m_scan_manager;
  ScanHeadSpec m_spec;
  StatusMessage m_status;
  jsScanHeadConfiguration m_config_default;
  jsScanHeadConfiguration m_config;
  jsDataFormat m_format;

  std::unique_ptr<TCPSocket> m_sock_ctrl;
  std::unique_ptr<TCPSocket> m_sock_data;

  jsScanHeadType m_type;
  jsUnits m_units;
  jsCableOrientation m_cable;

  jsRawProfile m_profiles[kMaxCircularBufferSize];
  moodycamel::BlockingReaderWriterCircularBuffer<jsRawProfile*>
    m_free_buffer;
  moodycamel::BlockingReaderWriterCircularBuffer<jsRawProfile*>
    m_ready_buffer;
  std::condition_variable m_new_data_cv;
  std::mutex m_new_data_mtx;

  flatbuffers::FlatBufferBuilder m_builder;
  std::map<std::pair<jsCamera,jsLaser>, AlignmentParams> m_map_alignment;
  std::map<std::pair<jsCamera,jsLaser>,
           std::shared_ptr<jsExclusionMask>> m_map_exclusion;
  std::map<std::pair<jsCamera,jsLaser>,
           std::shared_ptr<jsBrightnessCorrection_BETA>>
            m_map_brightness_correction;
  std::map<std::pair<jsCamera,jsLaser>, ScanWindow> m_map_window;
  std::vector<ScanPair> m_scan_pairs;
  std::condition_variable m_receive_thread_data_sync;
  std::thread m_receive_thread;
  std::mutex m_mutex;

  SemanticVersion m_firmware_version;
  uint32_t m_serial_number;
  uint32_t m_ip_address;
  std::string m_client_name;
  uint32_t m_client_ip_address;
  uint32_t m_id;
  int m_port;
  uint8_t *m_packet_buf;
  uint32_t m_packet_buf_len;
  uint32_t m_scan_period_us;
  uint32_t m_data_type_mask;
  uint32_t m_data_stride;
  uint32_t m_min_ecoder_travel;
  uint64_t m_idle_scan_period_ns;
  int64_t m_last_encoder;
  uint64_t m_last_timestamp;
  bool m_is_scanning;
};
} // namespace joescan

#endif // JOESCAN_SCAN_HEAD_H
