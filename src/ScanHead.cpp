/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "json.hpp"
#include "NetworkInterface.hpp"
#include "ScanHead.hpp"
#include "MessageClient_generated.h"
#include "MessageServer_generated.h"
#include "js50_spec_bin.h"

using namespace joescan;

#define CAMERA_GET_LASER(camera, laser) \
  if (joescan::schema::client::ConfigurationGroupPrimary_LASER == \
      m_spec.configuration_group_primary) { \
    return JS_ERROR_USE_CAMERA_FUNCTION; \
  } \
  (laser) = GetPairedLaser((camera)); \
  if (JS_LASER_INVALID == (laser)) { \
    return JS_ERROR_INVALID_ARGUMENT; \
  }

#define LASER_GET_CAMERA(laser, camera) \
  if (joescan::schema::client::ConfigurationGroupPrimary_CAMERA == \
      m_spec.configuration_group_primary) { \
    return JS_ERROR_USE_LASER_FUNCTION; \
  } \
  (camera) = GetPairedCamera((laser)); \
  if (JS_CAMERA_INVALID == (camera)) { \
    return JS_ERROR_INVALID_ARGUMENT; \
  }

ScanHead::ScanHead(ScanManager &manager, jsDiscovered &discovered, uint32_t id)
  : m_scan_manager(manager),
    m_format(JS_DATA_FORMAT_XY_BRIGHTNESS_FULL),
    m_type(discovered.type),
    m_cable(JS_CABLE_ORIENTATION_UPSTREAM),
    m_circ_buffer(kMaxCircularBufferSize),
    m_builder(512),
    m_firmware_version{ discovered.firmware_version_major,
                        discovered.firmware_version_minor,
                        discovered.firmware_version_patch },
    m_serial_number(discovered.serial_number),
    m_ip_address(discovered.ip_addr),
    m_id(id),
    m_control_tcp_fd(INVALID_SOCKET),
    m_data_tcp_fd(INVALID_SOCKET),
    m_port(0),
    m_scan_period_us(0),
    m_packets_received(0),
    m_packets_received_for_profile(0),
    m_complete_profiles_received(0),
    m_min_ecoder_travel(0),
    m_idle_scan_period_ns(0),
    m_last_encoder(0),
    m_last_timestamp(0),
    m_is_receive_thread_active(false),
    m_is_scanning(false)
{
  m_units = m_scan_manager.GetUnits();
  m_packet_buf = new uint8_t[kMaxPacketSize];
  m_packet_buf_len = kMaxPacketSize;

  // TODO: this should constants; maybe defined in scan head specification?
  // default configuration
  m_config_default.camera_exposure_time_min_us = 10000;
  m_config_default.camera_exposure_time_def_us = 500000;
  m_config_default.camera_exposure_time_max_us = 1000000;
  m_config_default.laser_on_time_min_us = 100;
  m_config_default.laser_on_time_def_us = 500;
  m_config_default.laser_on_time_max_us = 1000;
  m_config_default.laser_detection_threshold = 120;
  m_config_default.saturation_threshold = 800;
  m_config_default.saturation_percentage = 30;
  m_config = m_config_default;

  LoadScanHeadSpecification(m_type, &m_spec);

  double alignment_scale = 0;
  if (JS_UNITS_INCHES == m_units) {
    alignment_scale = 1.0;
  } else if (JS_UNITS_MILLIMETER != m_units) {
    alignment_scale = 25.4;
  } else {
    throw std::runtime_error("invalid jsUnits");
  }

  AlignmentParams alignment(alignment_scale);
  std::shared_ptr<jsBrightnessCorrection_BETA> correction;
  std::shared_ptr<jsExclusionMask> exclusion;
  ScanWindow window;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);

    correction = std::make_shared<jsBrightnessCorrection_BETA>();
    correction->offset = 0;
    for (uint32_t k = 0; k < JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN; k++) {
      correction->scale_factors[k] = 1.0;
    }

    exclusion = std::make_shared<jsExclusionMask>();
    memset(exclusion->bitmap, 0, sizeof(exclusion->bitmap));

    m_map_alignment[pair] = alignment;
    m_map_brightness_correction[pair] = correction;
    m_map_exclusion[pair] = exclusion;
    m_map_window[pair] = window;
  }
}

ScanHead::~ScanHead()
{
  if (IsScanning()) {
    StopScanning();
  }

  if (IsConnected()) {
    Disconnect();
  }

  delete[] m_packet_buf;
}

jsScanHeadType ScanHead::GetType() const
{
  return m_type;
}

uint32_t ScanHead::GetSerialNumber() const
{
  return m_serial_number;
}

uint32_t ScanHead::GetId() const
{
  return m_id;
}

uint32_t ScanHead::GetIpAddress() const
{
  return m_ip_address;
}

SemanticVersion ScanHead::GetFirmwareVersion()
{
  return m_firmware_version;
}

jsScanHeadCapabilities ScanHead::GetCapabilities()
{
  jsScanHeadCapabilities capabilities;

  capabilities.camera_brightness_bit_depth = 8; // hardcoded for now
  capabilities.max_camera_image_height = m_spec.max_camera_rows;
  capabilities.max_camera_image_width = m_spec.max_camera_columns;
  capabilities.max_scan_period_us = m_spec.max_scan_period_us;
  capabilities.min_scan_period_us = m_spec.min_scan_period_us;
  capabilities.num_cameras = m_spec.number_of_cameras;
  capabilities.num_encoders = 1; // hardcoded for now
  capabilities.num_lasers = m_spec.number_of_lasers;

  return capabilities;
}

int ScanHead::Connect(uint32_t timeout_s)
{
  using namespace schema::client;
  SOCKET fd = -1;
  int r = 0;

  m_mutex.lock();
  {
    net_iface iface =
      NetworkInterface::InitTCPSocket(m_ip_address, kScanServerPort, timeout_s);
    m_control_tcp_fd = iface.sockfd;
  }

  {
    net_iface iface =
      NetworkInterface::InitTCPSocket(m_ip_address, 12348, timeout_s);
    m_data_tcp_fd = iface.sockfd;
  }

  m_is_receive_thread_active = true;
  std::thread receive_thread(&ScanHead::ReceiveMain, this);
  m_receive_thread = std::move(receive_thread);

  m_builder.Clear();
  auto data_offset =
    CreateConnectData(m_builder, m_serial_number, m_id, ConnectionType_NORMAL);
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_CONNECT, MessageData_ConnectData,
                        data_offset.Union());
  m_builder.Finish(msg_offset);

  r = TCPSend(m_builder);
  if (0 != r) {
    m_mutex.unlock();
    return r;
  }

  // manually unlock; calling GetStatusMessage will lock the mutex again
  m_mutex.unlock();

  StatusMessage status;
  r = GetStatusMessage(&status);
  if (0 != r) {
    return r;
  }

  return 0;
}

int ScanHead::Disconnect(void)
{
  using namespace schema::client;

  m_mutex.lock();
  m_builder.Clear();

  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_DISCONNECT, MessageData_NONE);
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);
  m_is_receive_thread_active = false;
  NetworkInterface::CloseSocket(m_control_tcp_fd);
  m_control_tcp_fd = INVALID_SOCKET;
  NetworkInterface::CloseSocket(m_data_tcp_fd);
  m_data_tcp_fd = INVALID_SOCKET;
  m_mutex.unlock();
  m_receive_thread.join();

  return r;
}

int ScanHead::SendWindow()
{
  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    jsCamera camera = pair.first;
    jsLaser laser = pair.second;

    int r = SendWindow(camera, laser);
    if (0 != r) {
      return r;
    }
  }

  return 0;
}

int ScanHead::SendWindow(jsCamera camera)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SendWindow(camera, laser);
}

int ScanHead::SendWindow(jsLaser laser)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SendWindow(camera, laser);
}

int ScanHead::SendScanConfiguration()
{
  using namespace schema::client;

  if (0 == m_scan_pairs.size()) {
    // TODO: Do we return error? Or do we just silently let it fail?
    return 0;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  ScanConfigurationDataT cfg;
  cfg.udp_port = m_port;
  cfg.data_type_mask = m_data_type_mask;
  cfg.data_stride = m_data_stride;
  cfg.scan_period_ns = static_cast<uint32_t>(m_scan_period_us * 1000);
  cfg.laser_detection_threshold = m_config.laser_detection_threshold;
  cfg.saturation_threshold = m_config.saturation_threshold;
  cfg.saturation_percent = m_config.saturation_percentage;

  for (auto &el : m_scan_pairs) {
    jsCamera camera = el.camera;
    jsLaser laser = el.laser;

    std::unique_ptr<CameraLaserConfigurationT> c(new CameraLaserConfigurationT);
    c->camera_port = CameraIdToPort(camera);
    c->laser_port = LaserIdToPort(laser);
    c->laser_on_time_min_ns = el.config.laser_on_time_min_us * 1000;
    c->laser_on_time_def_ns = el.config.laser_on_time_def_us * 1000;
    c->laser_on_time_max_ns = el.config.laser_on_time_max_us * 1000;
    c->scan_end_offset_ns = el.end_offset_us * 1000;

    std::pair<jsCamera, jsLaser> pair(camera, laser);
    auto *alignment = &m_map_alignment[pair];

    if (m_spec.camera_port_cable_upstream == c->camera_port) {
      c->camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == m_cable) ?
                              CameraOrientation_UPSTREAM :
                              CameraOrientation_DOWNSTREAM;
    } else {
      c->camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == m_cable) ?
                              CameraOrientation_DOWNSTREAM :
                              CameraOrientation_UPSTREAM;
    }

    cfg.camera_laser_configurations.push_back(std::move(c));
  }

  m_builder.Clear();
  auto data_offset = ScanConfigurationData::Pack(m_builder, &cfg);
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_SCAN_CONFIGURATION,
                        MessageData_ScanConfigurationData, data_offset.Union());
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);

  return r;
}

int ScanHead::SendKeepAlive()
{
  using namespace schema::client;

  std::unique_lock<std::mutex> lock(m_mutex);
  m_builder.Clear();
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_KEEP_ALIVE, MessageData_NONE);
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);

  return r;
}

int ScanHead::StartScanning()
{
  using namespace schema::client;

  std::unique_lock<std::mutex> lock(m_mutex);
  m_profile = ProfileBuilder();
  m_packets_received = 0;
  m_complete_profiles_received = 0;
  // reset circular buffer holding profile data
  m_circ_buffer.clear();

  m_builder.Clear();
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_SCAN_START, MessageData_NONE);
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);

  if (0 == r) {
    m_is_scanning = true;
  }

  return r;
}

int ScanHead::StopScanning()
{
  using namespace schema::client;
  std::unique_lock<std::mutex> lock(m_mutex);
  m_builder.Clear();
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_SCAN_STOP, MessageData_NONE);
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);

  if (0 == r) {
    m_is_scanning = false;
  }

  return r;
}

bool ScanHead::IsConnected()
{
  return (INVALID_SOCKET != m_control_tcp_fd) ? true : false;
}

bool ScanHead::IsScanning()
{
  return m_is_scanning;
}

jsCamera ScanHead::GetPairedCamera(jsLaser laser)
{
  using namespace joescan::schema::client;
  if (ConfigurationGroupPrimary_CAMERA == m_spec.configuration_group_primary) {
    return JS_CAMERA_INVALID;
  }

  if (false == IsLaserValid(laser)) {
    return JS_CAMERA_INVALID;
  }

  uint32_t laser_port = LaserIdToPort(laser);
  jsCamera camera = JS_CAMERA_INVALID;
  for (auto &grp : m_spec.configuration_groups) {
    if (grp.laser_port() == laser_port) {
      camera = CameraPortToId(grp.camera_port());
    }
  }

  return camera;
}

jsLaser ScanHead::GetPairedLaser(jsCamera camera)
{
  using namespace joescan::schema::client;
  if (ConfigurationGroupPrimary_LASER == m_spec.configuration_group_primary) {
    return JS_LASER_INVALID;
  }

  if (false == IsCameraValid(camera)) {
    return JS_LASER_INVALID;
  }

  uint32_t camera_port = CameraIdToPort(camera);
  jsLaser laser = JS_LASER_INVALID;
  for (auto &grp : m_spec.configuration_groups) {
    if (grp.camera_port() == camera_port) {
      laser = LaserPortToId(grp.laser_port());
    }
  }

  return laser;
}

int32_t ScanHead::GetImage(jsCamera camera, uint32_t camera_exposure_us,
                           uint32_t laser_on_time_us, jsCameraImage *image)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return GetImage(camera, laser, camera_exposure_us, laser_on_time_us, image);
}

int32_t ScanHead::GetImage(jsLaser laser, uint32_t camera_exposure_us,
                           uint32_t laser_on_time_us, jsCameraImage *image)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return GetImage(camera, laser, camera_exposure_us, laser_on_time_us, image);
}

int32_t ScanHead::GetImage(jsCamera camera, jsLaser laser,
                           uint32_t camera_exposure_us,
                           uint32_t laser_on_time_us, jsCameraImage *image)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  // Only allow image capture if connected and not currently scanning.
  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  } else if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  {
    using namespace schema::client;
    ImageRequestDataT data;
    data.camera_port = camera_port;
    data.laser_port = laser_port;
    data.camera_exposure_ns = camera_exposure_us * 1000;
    data.laser_on_time_ns = laser_on_time_us * 1000;

    m_builder.Clear();
    auto data_offset = ImageRequestData::Pack(m_builder, &data);
    auto msg_offset =
      CreateMessageClient(m_builder, MessageType_IMAGE_REQUEST,
                          MessageData_ImageRequestData, data_offset.Union());
    m_builder.Finish(msg_offset);
    int r = TCPSend(m_builder);
    if (0 > r) {
      return r;
    }
  }

  {
    using namespace schema::server;
    // size of buffer was determined by measuring the size of flatbuffer
    // message returning the image data; if the size of the message increases
    // during development, you should see an `assert` message failure in
    // TCPRead where the framing message word indicating the message's size is
    // greater than the buffer length available to read the message into
    uint32_t buf_len = 0x200000;
    std::vector<uint8_t> buf(buf_len, 0);
    uint32_t len = buf_len;
    uint8_t *dst = &buf[0];
    uint32_t size = 0;

    do {
      int r = TCPRead(dst, len, &size, m_control_tcp_fd);
      if (0 > r) {
        return r;
      }
      // total length of buffer remaining at new offset
      len -= r;
      dst += r;
    } while (0 != size);

    // total length of data read out
    len = buf_len - len;
    auto verifier = flatbuffers::Verifier(&buf[0], len);
    if (!VerifyMessageServerBuffer(verifier)) {
      // not a flatbuffer message
      return JS_ERROR_INTERNAL;
    }

    // avoiding flatbuffer object API to avoid consuming extra memory
    auto msg = GetMessageServer(&buf[0]);
    if (MessageType_IMAGE != msg->type()) {
      // wrong / invalid message
      return JS_ERROR_INTERNAL;
    }

    auto data = msg->data_as_ImageData();
    if (nullptr == data) {
      // missing data
      return JS_ERROR_INTERNAL;
    }

    auto pixels = data->pixels();
    if (nullptr == pixels) {
      // missing data
      return JS_ERROR_INTERNAL;
    }

    if (pixels->size() != JS_CAMERA_IMAGE_DATA_LEN) {
      // incorrect data size
      return JS_ERROR_INTERNAL;
    }

    auto encoders = data->encoders();
    // need to be careful here, no scansync means no encoders; flatbuffer will
    // return a nullptr if it has no values
    uint32_t encoders_size = (nullptr == encoders) ? 0 : encoders->size();

    if (encoders_size > JS_ENCODER_MAX) {
      // incorrect data size
      return JS_ERROR_INTERNAL;
    }

    image->scan_head_id = m_id;
    image->timestamp_ns = data->timestamp_ns();
    image->camera = CameraPortToId(data->camera_port());
    image->laser = LaserPortToId(data->laser_port());
    // TODO: if we ever do image autoexposure grab these from the message
    image->camera_exposure_time_us = camera_exposure_us;
    image->laser_on_time_us = laser_on_time_us;
    image->image_height = data->height();
    image->image_width = data->width();
    image->num_encoder_values = encoders_size;

    for (uint32_t n = 0; n < pixels->size(); n++) {
      image->data[n] = pixels->Get(n);
    }

    for (uint32_t n = 0; n < encoders_size; n++) {
      image->encoder_values[n] = encoders->Get(n);
    }
  }

  return 0;
}

int32_t ScanHead::GetProfile(jsCamera camera, uint32_t camera_exposure_us,
                             uint32_t laser_on_time_us, jsRawProfile *profile)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return GetProfile(camera, laser, camera_exposure_us, laser_on_time_us,
                    profile);
}

int32_t ScanHead::GetProfile(jsLaser laser, uint32_t camera_exposure_us,
                             uint32_t laser_on_time_us, jsRawProfile *profile)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return GetProfile(camera, laser, camera_exposure_us, laser_on_time_us,
                    profile);
}

int32_t ScanHead::GetProfile(jsCamera camera, jsLaser laser,
                             uint32_t camera_exposure_us,
                             uint32_t laser_on_time_us,
                             jsRawProfile *profile)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  // Only allow image capture if connected and not currently scanning.
  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  } else if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  {
    using namespace schema::client;
    ProfileRequestDataT data;
    data.camera_port = camera_port;
    data.laser_port = laser_port;
    data.camera_exposure_ns = camera_exposure_us * 1000;
    data.laser_on_time_ns = laser_on_time_us * 1000;
    data.laser_detection_threshold = m_config.laser_detection_threshold;
    data.saturation_threshold = m_config.saturation_threshold;

    std::pair<jsCamera, jsLaser> pair(camera, laser);
    auto *alignment = &m_map_alignment[pair];


    if (m_spec.camera_port_cable_upstream == camera_port) {
      data.camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == m_cable) ?
                                CameraOrientation_UPSTREAM :
                                CameraOrientation_DOWNSTREAM;
    } else {
      data.camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == m_cable) ?
                                CameraOrientation_DOWNSTREAM :
                                CameraOrientation_UPSTREAM;
    }

    m_builder.Clear();

    auto data_offset = ProfileRequestData::Pack(m_builder, &data);
    auto msg_offset =
      CreateMessageClient(m_builder, MessageType_PROFILE_REQUEST,
                          MessageData_ProfileRequestData, data_offset.Union());
    m_builder.Finish(msg_offset);
    int r = TCPSend(m_builder);
    if(0 > r) {
      return r;
    }
  }

  {
    using namespace schema::server;

    uint32_t buf_len = 0x8000;
    std::vector<uint8_t> buf(buf_len, 0);
    uint32_t len = buf_len;
    uint8_t *dst = &buf[0];
    uint32_t size = 0;

    do {
      int r = TCPRead(dst, len, &size, m_control_tcp_fd);
      if (0 > r) {
        return r;
      }
      len -= r;
      dst += r;
    } while (0 != size);

    len = buf_len - len;
    auto verifier = flatbuffers::Verifier(&buf[0], len);
    if (!VerifyMessageServerBuffer(verifier)) {
      // not a message we recognize
      return JS_ERROR_INTERNAL;
    }

    auto msg = GetMessageServer(&buf[0]);
    if (MessageType_PROFILE != msg->type()) {
      // wrong / invalid message
      return JS_ERROR_INTERNAL;
    }

    auto data = msg->data_as_ProfileData();
    if (nullptr == data) {
      // missing data
      return JS_ERROR_INTERNAL;
    }

    auto points = data->points();
    auto encoders = data->encoders();
    // need to be careful here, no scansync means no encoders; flatbuffer will
    // return a nullptr if it has no values
    uint32_t encoders_size = (nullptr == encoders) ? 0 : encoders->size();

    if (encoders_size > JS_ENCODER_MAX) {
      // incorrect data size
      return JS_ERROR_INTERNAL;
    }

    profile->scan_head_id = m_id;
    profile->timestamp_ns = data->timestamp_ns();
    profile->camera = CameraPortToId(data->camera_port());
    profile->laser = LaserPortToId(data->laser_port());
    profile->laser_on_time_us = data->laser_on_time_ns() / 1000;
    profile->num_encoder_values = encoders_size;
    profile->packets_received = 0;
    profile->packets_expected = 0;
    profile->data_len = JS_RAW_PROFILE_DATA_LEN;
    profile->data_valid_brightness = data->valid_points();
    profile->data_valid_xy = data->valid_points();

    std::pair<jsCamera, jsLaser> pair(profile->camera, profile->laser);
    AlignmentParams *alignment = &m_map_alignment[pair];
    const int16_t INVALID_XY = -32768;

    for (uint32_t n = 0; n < points->size(); n++) {
      auto point = points->Get(n);
      int16_t x_raw = point->x();
      int16_t y_raw = point->y();
      uint8_t brightness = point->brightness();

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);

        Point2D<int32_t> p = alignment->CameraToMill(x, y);
        profile->data[n].x = p.x;
        profile->data[n].y = p.y;
        profile->data[n].brightness = brightness;
      } else {
        profile->data[n].x = JS_PROFILE_DATA_INVALID_XY;
        profile->data[n].y = JS_PROFILE_DATA_INVALID_XY;
        profile->data[n].brightness = JS_PROFILE_DATA_INVALID_BRIGHTNESS;
      }
    }

    for (uint32_t n = 0; n < encoders_size; n++) {
      profile->encoder_values[n] = encoders->Get(n);
    }
    profile->data_len = points->size();
  }
  return 0;
}

uint32_t ScanHead::AvailableProfiles()
{
  return static_cast<uint32_t>(m_circ_buffer.size());
}

uint32_t ScanHead::WaitUntilAvailableProfiles(uint32_t count,
                                              uint32_t timeout_us)
{
  std::chrono::duration<uint32_t, std::micro> timeout(timeout_us);
  std::unique_lock<std::mutex> lock(m_mutex);
  m_receive_thread_data_sync.wait_for(
    lock, timeout, [this, count] { return m_circ_buffer.size() >= count; });
  return static_cast<uint32_t>(m_circ_buffer.size());
}

std::vector<std::shared_ptr<jsRawProfile>> ScanHead::GetProfiles(uint32_t count)
{
  std::vector<std::shared_ptr<jsRawProfile>> profiles;
  std::shared_ptr<jsRawProfile> profile = nullptr;
  std::lock_guard<std::mutex> lock(m_mutex);

  while (!m_circ_buffer.empty() && (0 < count)) {
    profile = m_circ_buffer.front();
    m_circ_buffer.pop_front();

    profiles.push_back(profile);
    count--;
  }

  return profiles;
}

void ScanHead::ClearProfiles()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_circ_buffer.clear();
}

int ScanHead::GetStatusMessage(StatusMessage *status)
{
  static const int32_t buf_len = 256;
  static uint8_t buf[buf_len];
  int r = -1;

  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  }

  {
    using namespace schema::client;

    // Just need to lock here since the only shared resources are the TCP
    // socket and the flat buffer builder
    std::unique_lock<std::mutex> lock(m_mutex);
    m_builder.Clear();

    auto msg_offset = CreateMessageClient(m_builder, MessageType_STATUS_REQUEST,
                                          MessageData_NONE);

    m_builder.Finish(msg_offset);

    r = TCPSend(m_builder);
    if (0 != r) {
      return r;
    }

    r = TCPRead(buf, buf_len, m_control_tcp_fd);
    if (0 > r) {
      return r;
    }
  }

  {
    using namespace schema::server;

    uint32_t len = static_cast<uint32_t>(r);
    auto verifier = flatbuffers::Verifier(buf, len);
    if (!VerifyMessageServerBuffer(verifier)) {
      // not a flatbuffer message
      return JS_ERROR_INTERNAL;
    }

    auto msg = UnPackMessageServer(buf);
    if (MessageType_STATUS != msg->type) {
      return JS_ERROR_INTERNAL;
    }

    auto data = msg->data.AsStatusData();
    if (nullptr == data) {
      return JS_ERROR_INTERNAL;
    }

    memset(&m_status, 0, sizeof(StatusMessage));

    m_status.user.global_time_ns = data->global_time_ns;
    m_status.user.num_profiles_sent = data->num_profiles_sent;

    for (auto &c : data->camera_data) {
      jsCamera camera = CameraPortToId(c->port);
      if (JS_CAMERA_A == camera) {
        m_status.user.camera_a_pixels_in_window = c->pixels_in_window;
        m_status.user.camera_a_temp = c->temperature;
      } else if (JS_CAMERA_B == camera) {
        m_status.user.camera_b_pixels_in_window = c->pixels_in_window;
        m_status.user.camera_b_temp = c->temperature;
      }
    }

    m_status.user.num_encoder_values = data->encoders.size();
    std::copy(data->encoders.begin(), data->encoders.end(),
              m_status.user.encoder_values);

    m_status.min_scan_period_us = data->min_scan_period_ns / 1000;

    *status = m_status;
  }

  return 0;
}

StatusMessage ScanHead::GetLastStatusMessage()
{
  return m_status;
}

void ScanHead::ClearStatusMessage()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  memset(&m_status, 0, sizeof(StatusMessage));
}

ScanManager &ScanHead::GetScanManager()
{
  return m_scan_manager;
}

bool ScanHead::IsConfigurationValid(jsScanHeadConfiguration &cfg)
{
  if ((cfg.camera_exposure_time_max_us > m_spec.max_camera_exposure_us) ||
      (cfg.camera_exposure_time_min_us < m_spec.min_camera_exposure_us) ||
      (cfg.camera_exposure_time_max_us < cfg.camera_exposure_time_def_us) ||
      (cfg.camera_exposure_time_max_us < cfg.camera_exposure_time_min_us) ||
      (cfg.camera_exposure_time_def_us < cfg.camera_exposure_time_min_us)) {
    return false;
  }

  if ((cfg.laser_on_time_max_us > m_spec.max_laser_on_time_us) ||
      (cfg.laser_on_time_min_us < m_spec.min_laser_on_time_us) ||
      (cfg.laser_on_time_max_us < cfg.laser_on_time_def_us) ||
      (cfg.laser_on_time_max_us < cfg.laser_on_time_min_us) ||
      (cfg.laser_on_time_def_us < cfg.laser_on_time_min_us)) {
    return false;
  }

  if (cfg.laser_detection_threshold > kMaxLaserDetectionThreshold) {
    return false;
  }

  if (cfg.saturation_threshold > kMaxSaturationThreshold) {
    return false;
  }

  if (cfg.saturation_percentage > kMaxSaturationPercentage) {
    return false;
  }

  return true;
}

int ScanHead::SetConfiguration(jsScanHeadConfiguration &cfg)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  if (!IsConfigurationValid(cfg)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_config = cfg;

  return 0;
}

jsScanHeadConfiguration ScanHead::GetConfiguration() const
{
  return m_config;
}

jsScanHeadConfiguration ScanHead::GetConfigurationDefault() const
{
  return m_config_default;
}

int ScanHead::SetDataFormat(jsDataFormat format)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  switch (format) {
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_FULL):
      m_data_type_mask = DataType::XYData | DataType::Brightness;
      m_data_stride = 1;
      break;
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_HALF):
      m_data_type_mask = DataType::XYData | DataType::Brightness;
      m_data_stride = 2;
      break;
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_QUARTER):
      m_data_type_mask = DataType::XYData | DataType::Brightness;
      m_data_stride = 4;
      break;
    case (JS_DATA_FORMAT_XY_FULL):
      m_data_type_mask = DataType::XYData;
      m_data_stride = 1;
      break;
    case (JS_DATA_FORMAT_XY_HALF):
      m_data_type_mask = DataType::XYData;
      m_data_stride = 2;
      break;
    case (JS_DATA_FORMAT_XY_QUARTER):
      m_data_type_mask = DataType::XYData;
      m_data_stride = 4;
      break;
    default:
      return JS_ERROR_INVALID_ARGUMENT;
  }

  m_format = format;
  return 0;
}

jsDataFormat ScanHead::GetDataFormat() const
{
  return m_format;
}

int ScanHead::SetScanPeriod(uint32_t period_us)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  if ((period_us > m_spec.max_scan_period_us) ||
      (period_us < m_spec.min_scan_period_us)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_scan_period_us = period_us;
  return 0;
}

uint32_t ScanHead::GetScanPeriod() const
{
  return m_scan_period_us;
}

uint32_t ScanHead::GetMinScanPeriod()
{
  uint32_t p = (m_status.min_scan_period_us < m_spec.min_scan_period_us) ?
               m_spec.min_scan_period_us :
               m_status.min_scan_period_us;

  return p;
}

void ScanHead::ResetScanPairs()
{
  m_scan_pairs.clear();
}

int ScanHead::AddScanPair(jsCamera camera, jsLaser laser,
                          jsScanHeadConfiguration &cfg, uint32_t end_offset_us)
{
  if (false == IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (!(IsConfigurationValid(cfg))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (m_scan_pairs.size() >= m_spec.max_configuration_groups) {
    return JS_ERROR_INTERNAL;
  }

  ScanPair el;
  el.camera = camera;
  el.laser = laser;
  el.config = cfg;
  el.end_offset_us = end_offset_us;

  m_scan_pairs.push_back(el);

  return 0;
}

uint32_t ScanHead::GetMaxScanPairs()
{
  return m_spec.max_configuration_groups;
}

int ScanHead::SetCableOrientation(jsCableOrientation cable)
{
  if ((JS_CABLE_ORIENTATION_UPSTREAM != cable) &&
      (JS_CABLE_ORIENTATION_DOWNSTREAM != cable)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_cable = cable;

  for (auto &m : m_map_alignment) {
    AlignmentParams &a = m.second;
    a.SetCableOrientation(cable);
  }

  return 0;
}

jsCableOrientation ScanHead::GetCableOrientation()
{
  return m_cable;
}

int ScanHead::SetAlignment(double roll_degrees, double shift_x, double shift_y)
{
  using namespace schema::client;
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SetAlignment(pair.first, pair.second, roll_degrees, shift_x, shift_y);
  }

  return r;
}

int ScanHead::SetAlignment(jsCamera camera, double roll_degrees, double shift_x,
                           double shift_y)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::SetAlignment(jsLaser laser, double roll_degrees, double shift_x,
                           double shift_y)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::GetAlignment(jsCamera camera, double *roll_degrees,
                           double *shift_x, double *shift_y)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return GetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::GetAlignment(jsLaser laser, double *roll_degrees, double *shift_x,
                           double *shift_y)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return GetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::SetExclusionMask(jsExclusionMask *mask)
{
  int r = 0;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SetExclusionMask(pair.first, pair.second, mask);
  }

  return r;
}

int ScanHead::SetExclusionMask(jsCamera camera, jsExclusionMask *mask)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SetExclusionMask(camera, laser, mask);
}

int ScanHead::SetExclusionMask(jsLaser laser, jsExclusionMask *mask)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SetExclusionMask(camera, laser, mask);
}

int ScanHead::GetExclusionMask(jsCamera camera, jsExclusionMask *mask)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return GetExclusionMask(camera, laser, mask);
}

int ScanHead::GetExclusionMask(jsLaser laser, jsExclusionMask *mask)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return GetExclusionMask(camera, laser, mask);
}

int ScanHead::SendExclusionMask()
{
  int r = 0;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SendExclusionMask(pair.first, pair.second);
  }

  return r;
}

int ScanHead::SendExclusionMask(jsCamera camera)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SendExclusionMask(camera, laser);
}

int ScanHead::SendExclusionMask(jsLaser laser)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SendExclusionMask(camera, laser);
}

int ScanHead::SetBrightnessCorrection(jsCamera camera,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SetBrightnessCorrection(camera, laser, correction);
}

int ScanHead::SetBrightnessCorrection(jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SetBrightnessCorrection(camera, laser, correction);
}

int ScanHead::GetBrightnessCorrection(jsCamera camera,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return GetBrightnessCorrection(camera, laser, correction);
}

int ScanHead::GetBrightnessCorrection(jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return GetBrightnessCorrection(camera, laser, correction);
}

int ScanHead::SendBrightnessCorrection()
{
  int r = 0;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SendBrightnessCorrection(pair.first, pair.second);
  }

  return r;
}

int ScanHead::SendBrightnessCorrection(jsCamera camera)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SendBrightnessCorrection(camera, laser);
}

int ScanHead::SendBrightnessCorrection(jsLaser laser)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SendBrightnessCorrection(camera, laser);
}

uint32_t ScanHead::GetMinimumEncoderTravel()
{
  return m_min_ecoder_travel;
}

int32_t ScanHead::SetMinimumEncoderTravel(uint32_t travel)
{
  m_min_ecoder_travel = travel;

  return 0;
}

uint32_t ScanHead::GetIdleScanPeriod()
{
  return m_idle_scan_period_ns / 1000;
}

int32_t ScanHead::SetIdleScanPeriod(uint32_t period_us)
{
  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  m_idle_scan_period_ns = period_us * 1000;

  return 0;
}

int ScanHead::SetWindow(ScanWindow &window)
{
  using namespace schema::client;
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SetWindow(pair.first, pair.second, window);
  }

  return r;
}

int ScanHead::SetWindow(jsCamera camera, ScanWindow &window)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SetWindow(camera, laser, window);
}

int ScanHead::SetWindow(jsLaser laser, ScanWindow &window)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SetWindow(camera, laser, window);
}

int ScanHead::SetPolygonWindow(jsCoordinate *points, uint32_t points_len)
{
  using namespace schema::client;
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  for (uint32_t n = CameraLaserIdxBegin(); n < CameraLaserIdxEnd(); n++) {
    auto pair = CameraLaserNext(n);
    r = SetPolygonWindow(pair.first, pair.second, points, points_len);
    if (0 != r) {
      return r;
    }
  }

  return r;
}

int ScanHead::SetPolygonWindow(jsCamera camera, jsCoordinate *points,
                               uint32_t points_len)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return SetPolygonWindow(camera, laser, points, points_len);
}

int ScanHead::SetPolygonWindow(jsLaser laser, jsCoordinate *points,
                     uint32_t points_len)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return SetPolygonWindow(camera, laser, points, points_len);
}

int ScanHead::GetWindow(jsCamera camera, ScanWindow *window)
{
  jsLaser laser = GetPairedLaser(camera);
  if (JS_LASER_INVALID == laser) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  return GetWindow(camera, laser, window);
}

int ScanHead::GetWindow(jsLaser laser, ScanWindow *window)
{
  jsCamera camera = GetPairedCamera(laser);
  if (JS_CAMERA_INVALID == camera) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  return GetWindow(camera, laser, window);
}

bool ScanHead::IsPairValid(jsCamera camera, jsLaser laser)
{
  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return false;
  }
  uint32_t camera_port = (uint32_t)tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return false;
  }
  uint32_t laser_port = (uint32_t)tmp;

  for (uint32_t n = 0; n < m_spec.configuration_groups.size(); n++) {
    uint32_t camera_cmp = m_spec.configuration_groups[n].camera_port();
    uint32_t laser_cmp = m_spec.configuration_groups[n].laser_port();

    if ((camera_port == camera_cmp) && (laser_port == laser_cmp)) {
      return true;
    }
  }

  return false;
}

bool ScanHead::IsCameraValid(jsCamera camera)
{
  if (JS_CAMERA_INVALID >= camera) {
    return false;
  }

  // subtract to account for valid cameras begining at 1
  uint32_t val = ((uint32_t)camera) - 1;
  return ((val < m_spec.number_of_cameras) ? true : false);
}

int ScanHead::GetAlignment(jsCamera camera, jsLaser laser, double *roll_degrees,
                           double *shift_x, double *shift_y)
{
  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  AlignmentParams *alignment = &m_map_alignment[pair];

  *roll_degrees = alignment->GetRoll();
  *shift_x = alignment->GetShiftX();
  *shift_y = alignment->GetShiftY();

  return 0;
}

int ScanHead::SetAlignment(jsCamera camera, jsLaser laser, double roll_degrees,
                           double shift_x, double shift_y)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  double alignment_scale = 0;
  if (JS_UNITS_INCHES == m_units) {
    alignment_scale = 1.0;
  } else if (JS_UNITS_MILLIMETER != m_units) {
    alignment_scale = 25.4;
  } else {
    return JS_ERROR_INTERNAL;
  }

  AlignmentParams alignment(alignment_scale, roll_degrees, shift_x, shift_y,
                            m_cable);

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  m_map_alignment[pair] = alignment;

  return 0;
}

int ScanHead::GetExclusionMask(jsCamera camera, jsLaser laser,
                               jsExclusionMask *mask)
{
  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  *mask = *(m_map_exclusion[pair]);

  return 0;
}

int ScanHead::SetExclusionMask(jsCamera camera, jsLaser laser,
                               jsExclusionMask *mask)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    return JS_ERROR_VERSION_COMPATIBILITY;
  }

  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::unique_lock<std::mutex> lock(m_mutex);

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  *(m_map_exclusion[pair]) = *mask;

  return 0;
}

int ScanHead::SendExclusionMask(jsCamera camera, jsLaser laser)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    // scan head doesn't support, don't send
    return 0;
  }

  if (!IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  jsExclusionMask *mask = m_map_exclusion[pair].get();

  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  using namespace schema::client;
  ExclusionMaskDataT data;
  uint8_t byte = 0;
  uint32_t b = 0;
  // increment row
  for (uint32_t m = 0; m < JS_CAMERA_IMAGE_DATA_MAX_HEIGHT; m++) {
    // increment column
    for (uint32_t n = 0; n < JS_CAMERA_IMAGE_DATA_MAX_WIDTH; n++) {
      if (0 != mask->bitmap[m][n]) {
        byte |= 1 << (7 - b);
      }

      b++;

      if (8 == b) {
        data.mask.push_back(byte);
        byte = 0;
        b = 0;
      }
    }
  }

  data.camera_port = camera_port;
  data.laser_port = laser_port;

  m_builder.Clear();

  auto data_offset = ExclusionMaskData::Pack(m_builder, &data);
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_EXCLUSION_MASK,
                        MessageData_ExclusionMaskData, data_offset.Union());
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);
  if(0 > r) {
    return r;
  }

  return 0;
}

int ScanHead::GetBrightnessCorrection(jsCamera camera,
                                      jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    return JS_ERROR_VERSION_COMPATIBILITY;
  }

  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  *correction = *(m_map_brightness_correction[pair]);

  return 0;
}

int ScanHead::SetBrightnessCorrection(jsCamera camera,
                                      jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    return JS_ERROR_VERSION_COMPATIBILITY;
  }

  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::unique_lock<std::mutex> lock(m_mutex);

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  *(m_map_brightness_correction[pair]) = *correction;

  return 0;
}

int ScanHead::SendBrightnessCorrection(jsCamera camera, jsLaser laser)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    // scan head doesn't support, don't send
    return 0;
  }

  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  jsBrightnessCorrection_BETA *corr = m_map_brightness_correction[pair].get();

  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  using namespace schema::client;
  BrightnessCorrectionDataT data;
  data.camera_port = camera_port;
  data.laser_port = laser_port;
  data.image_offset = corr->offset;
  for (uint32_t n = 0; n < JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN; n++) {
    data.scale_factors.push_back(corr->scale_factors[n]);
  }

  m_builder.Clear();

  auto data_offset = BrightnessCorrectionData::Pack(m_builder, &data);
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_BRIGHTNESS_CORRECTION,
                        MessageData_BrightnessCorrectionData,
                        data_offset.Union());
  m_builder.Finish(msg_offset);
  int r = TCPSend(m_builder);
  if(0 > r) {
    return r;
  }

  return 0;
}

bool ScanHead::IsLaserValid(jsLaser laser)
{
  if (JS_LASER_INVALID >= laser) {
    return false;
  }

  // subtract to account for valid lasers begining at 1
  uint32_t val = ((uint32_t)laser) - 1;
  return ((val < m_spec.number_of_lasers) ? true : false);
}

int ScanHead::GetWindow(jsCamera camera, jsLaser laser, ScanWindow *window)
{
  if ((false == IsCameraValid(camera)) || (false == IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::unique_lock<std::mutex> lock(m_mutex);

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);

  if (m_map_window.find(pair) == m_map_window.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  window = &(m_map_window[pair]);

  return 0;
}

int ScanHead::SetWindow(jsCamera camera, jsLaser laser, ScanWindow &window)
{
  if (!IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::unique_lock<std::mutex> lock(m_mutex);

  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  m_map_window[pair] = window;

  return 0;
}

int ScanHead::SetPolygonWindow(jsCamera camera, jsLaser laser,
                               jsCoordinate *points, uint32_t points_len)
{
  if (!IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::vector<jsCoordinate> c(points, points + points_len);

  // check for clockwise point ordering - https://stackoverflow.com/a/18472899
  double sum = 0.0;
  jsCoordinate p1 = c[points_len - 1];
  for (uint32_t n = 0; n < points_len; n++) {
    jsCoordinate p2 = c[n];
    sum += (p2.x - p1.x) * (p2.y + p1.y);
    p1 = p2;
  }

  // polygon is clockwise if sum is greater than zero
  if (0.0 >= sum) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  // check for convexity - https://stackoverflow.com/a/1881201
  // add the first two points to the end of the list
  // to make loop cleaner since we have to calculate
  // (p[N-2],p[N-1],p[0]) and (p[N-1],p[0],p[1]).
  c.push_back(c[0]);
  c.push_back(c[1]);

  std::vector<double> product;
  for (uint32_t n = 0; n < points_len; n++) {
    double dx1 = c[n + 1].x - c[n + 0].x;
    double dy1 = c[n + 1].y - c[n + 0].y;
    double dx2 = c[n + 2].x - c[n + 1].x;
    double dy2 = c[n + 2].y - c[n + 1].y;
    product.push_back((dx1 * dy2) - (dy1 * dx2));
  }

  // polygon is convex if the sign of all cross products is the same
  bool is_negative = (product[0] < 0.0) ? true : false;
  for (auto &p : product) {
    if (0.0 == p) {
      return JS_ERROR_INVALID_ARGUMENT;
    } else if (is_negative && (0.0 < p)) {
      return JS_ERROR_INVALID_ARGUMENT;
    } else if (!is_negative && (0.0 > p)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }
  }

  // remove the two extra points used to check cross product
  c.pop_back();
  c.pop_back();

  std::pair<jsCamera, jsLaser> pair(camera, laser);
  ScanWindow window(c);
  m_map_window[pair] = window;

  return 0;
}

int ScanHead::SendWindow(jsCamera camera, jsLaser laser)
{
  using namespace schema::client;

  std::unique_lock<std::mutex> lock(m_mutex);
  WindowConfigurationDataT data;
  int port = 0;
  int r = 0;

  port = CameraIdToPort(camera);
  if (-1 == port) {
    return JS_ERROR_INTERNAL;
  }
  data.camera_port = port;

  port = LaserIdToPort(laser);
  if (-1 == port) {
    return JS_ERROR_INTERNAL;
  }
  data.laser_port = port;

  auto pair = std::make_pair(camera, laser);
  auto *alignment = &m_map_alignment[pair];
  auto *window = &m_map_window[pair];

  std::vector<WindowConstraint> constraints = window->GetConstraints();
  for (auto const &c : constraints) {
    Point2D<int32_t> p0, p1;
    std::unique_ptr<ConstraintT> constraint(new ConstraintT);

    // Note: units are in 1/1000 inch
    // calculate the first point of our window constraint
    p0.x = static_cast<int32_t>(c.constraints[0].x);
    p0.y = static_cast<int32_t>(c.constraints[0].y);
    // convert the point to the camera's coordinate system
    p0 = alignment->MillToCamera(p0.x, p0.y);
    // calculate the second point of out window constraint
    p1.x = static_cast<int32_t>(c.constraints[1].x);
    p1.y = static_cast<int32_t>(c.constraints[1].y);
    // convert the point to the camera's coordinate system
    p1 = alignment->MillToCamera(p1.x, p1.y);
    // pass constraint points to message to create the constraint
    if (JS_CABLE_ORIENTATION_DOWNSTREAM == m_cable) {
      constraint->x0 = p0.x;
      constraint->y0 = p0.y;
      constraint->x1 = p1.x;
      constraint->y1 = p1.y;
    } else {
      constraint->x0 = p1.x;
      constraint->y0 = p1.y;
      constraint->x1 = p0.x;
      constraint->y1 = p0.y;
    }

    data.constraints.push_back(std::move(constraint));
  }

  m_builder.Clear();
  auto data_offset = WindowConfigurationData::Pack(m_builder, &data);
  auto msg_offset = CreateMessageClient(
    m_builder, MessageType_WINDOW_CONFIGURATION,
    MessageData_WindowConfigurationData, data_offset.Union());
  m_builder.Finish(msg_offset);
  r = TCPSend(m_builder);
  if (0 != r) {
    return r;
  }

  return 0;
}

void ScanHead::LoadScanHeadSpecification(jsScanHeadType type,
                                         ScanHeadSpec *spec)
{
  uint8_t *bin;
  uint32_t bin_len;
  switch (type) {
  case (JS_SCAN_HEAD_JS50WX):
    bin = (uint8_t *)js50wx_spec;
    bin_len = js50wx_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50WSC):
    bin = (uint8_t *)js50wsc_spec;
    bin_len = js50wsc_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50X6B20):
    bin = (uint8_t *)js50x6b20_spec;
    bin_len = js50x6b20_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50X6B30):
    bin = (uint8_t *)js50x6b30_spec;
    bin_len = js50x6b30_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50MX):
    bin = (uint8_t *)js50mx_spec;
    bin_len = js50mx_spec_len;
    break;
  default:
    throw std::runtime_error("invalid jsScanHeadType");
  }

  {
    using namespace joescan::schema::client;
    auto msg = GetScanHeadSpecification(bin);
    msg->UnPackTo(spec);
  }
}

uint32_t ScanHead::CameraLaserIdxBegin()
{
  return 0;
}

uint32_t ScanHead::CameraLaserIdxEnd()
{
  using namespace joescan::schema::client;
  uint32_t idx = 0;

  switch (m_spec.configuration_group_primary) {
  case ConfigurationGroupPrimary_CAMERA:
    idx = m_spec.number_of_cameras;
    break;

  case ConfigurationGroupPrimary_LASER:
    idx = m_spec.number_of_lasers;
    break;

  case ConfigurationGroupPrimary_INVALID:
  default:
    // do nothing, leave invalid
    (void)idx;
  }

  return idx;
}

std::pair<jsCamera, jsLaser> ScanHead::CameraLaserNext(uint32_t idx)
{
  using namespace joescan::schema::client;
  jsCamera camera = JS_CAMERA_INVALID;
  jsLaser laser = JS_LASER_INVALID;

  switch (m_spec.configuration_group_primary) {
  case ConfigurationGroupPrimary_CAMERA:
    if (idx < m_spec.number_of_cameras) {
      camera = static_cast<jsCamera>(JS_CAMERA_A + idx);
      laser = GetPairedLaser(camera);
    }
    break;

  case ConfigurationGroupPrimary_LASER:
    if (idx < m_spec.number_of_lasers) {
      laser = static_cast<jsLaser>(JS_LASER_1 + idx);
      camera = GetPairedCamera(laser);
    }
    break;

  case ConfigurationGroupPrimary_INVALID:
  default:
    // do nothing, leave invalid
    (void)camera;
    (void)laser;
  }

  return std::make_pair(camera, laser);
}

void ScanHead::ProcessProfile(uint8_t *buf, uint32_t len)
{
  // private function, assume mutex is already locked
  DataPacket packet(buf, len, 0);
  uint32_t source = 0;
  uint64_t timestamp = 0;
  uint32_t raw_len = 0;
  uint8_t *raw = packet.GetRawBytes(&raw_len);
  const uint32_t total_packets = packet.GetNumParts();
  const uint32_t current_packet = packet.GetPartNum();
  const uint16_t datatype_mask = packet.GetContents();

  m_packets_received++;
  source = packet.GetSourceId();
  timestamp = packet.GetTimeStamp();

  if ((source != m_profile.Source()) ||
      (timestamp != m_profile.Timestamp())) {
    if (false == m_profile.IsEmpty()) {
      std::unique_lock<std::mutex> lock(m_mutex);
      // have a partial profile, push it back despite loss
      m_profile.SetPacketInfo(m_packets_received_for_profile, total_packets);
      PushProfile(m_profile.raw);
    }

    m_packets_received_for_profile = 0;

    jsCamera camera = CameraPortToId(packet.GetCameraPort());
    jsLaser laser = LaserPortToId(packet.GetLaserPort());
    m_profile = ProfileBuilder(camera, laser, packet, m_format);
  }

  // server sends int16_t x/y data points; invalid is int16_t minimum
  const int16_t INVALID_XY = -32768;
  const jsCamera camera = m_profile.raw->camera;
  const jsLaser laser = m_profile.raw->laser;
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  AlignmentParams *alignment = &m_map_alignment[pair];

  // if Brightness, assume X/Y data is present
  if (datatype_mask & DataType::Brightness) {
    FragmentLayout b_layout = packet.GetFragmentLayout(DataType::Brightness);
    FragmentLayout xy_layout = packet.GetFragmentLayout(DataType::XYData);
    uint8_t *b_src = reinterpret_cast<uint8_t *>(&(raw[b_layout.offset]));
    int16_t *xy_src = reinterpret_cast<int16_t *>(&(raw[xy_layout.offset]));
    const uint32_t start_column = packet.GetStartColumn();
    const jsCamera id = m_profile.raw->camera;

    // assume step is the same for both layouts
    const uint32_t inc = total_packets * xy_layout.step;
    uint32_t idx = start_column + current_packet * xy_layout.step;

    // assume num_vals is same for both layouts
    for (uint32_t n = 0; n < xy_layout.num_vals; n++) {
      int16_t x_raw = htons(*xy_src++);
      int16_t y_raw = htons(*xy_src++);
      uint8_t brightness = *b_src++;

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);

        Point2D<int32_t> point = alignment->CameraToMill(x, y);
        m_profile.InsertPointAndBrightness(idx, point, brightness);
      }

      idx += inc;
    }
  } else if (datatype_mask & DataType::XYData) {
    FragmentLayout layout = packet.GetFragmentLayout(DataType::XYData);
    int16_t *src = reinterpret_cast<int16_t *>(&(raw[layout.offset]));
    const uint32_t start_column = packet.GetStartColumn();
    const jsCamera id = m_profile.raw->camera;

    const uint32_t inc = total_packets * layout.step;
    uint32_t idx = start_column + current_packet * layout.step;

    for (unsigned int j = 0; j < layout.num_vals; j++) {
      int16_t x_raw = htons(*src++);
      int16_t y_raw = htons(*src++);

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);

        Point2D<int32_t> point = alignment->CameraToMill(x, y);
        m_profile.InsertPoint(idx, point);
      }

      idx += inc;
    }
  }

#if 0
  // we don't support this
  else if (datatype_mask & DataType::Subpixel) {
    FragmentLayout layout = packet.GetFragmentLayout(DataType::Subpixel);
    uint32_t m = 0;
    uint32_t n = layout.offset;
    uint16_t pixel = 0;

    for (unsigned int j = 0; j < layout.num_vals; j++) {
      pixel = htons(*(reinterpret_cast<uint16_t *>(&(raw[n]))));
      n += sizeof(uint16_t);

      if (kInvalidSubpixel != pixel) {
        m = packet.GetStartColumn();
        m += (j * total_packets + current_packet) * layout.step;

        Point2D point(pixel, m);
        m_profile.InsertPixelCoordinate(m, point);
      }
    }
  }
#endif

  m_packets_received_for_profile++;
  if (m_packets_received_for_profile == total_packets) {
    std::unique_lock<std::mutex> lock(m_mutex);
    // received all packets for the profile
    m_profile.SetPacketInfo(total_packets, total_packets);
    PushProfile(m_profile.raw);
    m_profile = ProfileBuilder();
    m_complete_profiles_received++;
  }
}

void ScanHead::PushProfile(std::shared_ptr<jsRawProfile> raw)
{
  if ((0 < m_min_ecoder_travel) && (0 < raw->num_encoder_values)) {
    uint32_t travel = std::abs(raw->encoder_values[0] - m_last_encoder);
    if (travel < m_min_ecoder_travel) {
      if (0 == m_idle_scan_period_ns) {
        return;
      }

      uint32_t diff_ns = raw->timestamp_ns - m_last_timestamp;
      if (diff_ns < m_idle_scan_period_ns) {
        return;
      }
    } else {
      m_last_encoder = raw->encoder_values[0];
    }

    m_last_timestamp = raw->timestamp_ns;
  }

  m_circ_buffer.push_back(raw);
  m_receive_thread_data_sync.notify_all();
}

void ScanHead::ReceiveMain()
{
#ifndef __linux__
  // bump up thread priority; receiving profiles is the most important thing
  // for end users
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

  while (m_is_receive_thread_active) {
    uint8_t *buf = m_packet_buf;
#ifdef __linux__
    size_t buf_len = m_packet_buf_len;
#else
    int buf_len = m_packet_buf_len;
#endif

    uint32_t total_len = 0;
    char *dst = reinterpret_cast<char *>(&total_len);
    int r = 0;

    uint32_t len = 0;
    while (m_is_receive_thread_active && (len < sizeof(uint32_t))) {
      r = recv(m_data_tcp_fd, dst + len, sizeof(uint32_t) - len, 0);
      if (0 > r) {
        if (!m_is_receive_thread_active) {
          return;
        }

        if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        throw std::runtime_error("Recv " + std::to_string(errno) + " " +
                                 std::strerror(errno));
      }
      else if (0 == r) {
          // connection closed
          return;
      }
      else {
          len += r;
      }
    }
    if (total_len > m_packet_buf_len) {
      throw std::runtime_error("Recv Out of Sync Requested " +
                                std::to_string(total_len) + " bytes");
    }

    len = 0;
    while (m_is_receive_thread_active && (len < total_len)) {
      dst = reinterpret_cast<char*>(buf + len);
      r = recv(m_data_tcp_fd, dst, total_len - len, 0);
      if (0 > r) {
        if (!m_is_receive_thread_active) {
          return;
        }

        if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        throw std::runtime_error("Recv " + std::to_string(errno) + " " +
                                 std::strerror(errno));
      } else if (0 == r) {
        // connection closed
        return;
      } else {
        len += r;
      }
    }

    if (m_is_receive_thread_active) {
      uint16_t magic = (buf[0] << 8) | (buf[1]);
      if (kDataMagic == magic) {
        ProcessProfile(buf, len);
      }
    }
  }
}

int ScanHead::TCPSend(flatbuffers::FlatBufferBuilder &builder)
{
  // private function, assume mutex is already locked
  char *msg = reinterpret_cast<char *>(builder.GetBufferPointer());
  uint32_t msg_len = builder.GetSize();
  SOCKET fd = m_control_tcp_fd;
  int r = 0;

  // NOTE: sending little-endian as to keep with approach used by Flatbuffers
  r = send(fd, reinterpret_cast<char *>(&msg_len), sizeof(uint32_t), 0);
  if (sizeof(uint32_t) != r) {
    return JS_ERROR_INTERNAL;
  }

  r = send(fd, msg, msg_len, 0);
  if (r != static_cast<int>(builder.GetSize())) {
    return JS_ERROR_INTERNAL;
  }

  return 0;
}

int ScanHead::TCPRead(uint8_t *buf, uint32_t len, SOCKET fd)
{
  uint32_t msg_len = 0;
  int r = 0;

  // NOTE: receiving little-endian as to keep with approach used by Flatbuffers
  r = recv(fd, reinterpret_cast<char *>(&msg_len), sizeof(uint32_t), 0);
  if (sizeof(uint32_t) != r) {
    return JS_ERROR_INTERNAL;
  }

  assert(msg_len < len);

  r = recv(fd, reinterpret_cast<char *>(buf), msg_len, 0);
  if (static_cast<int>(msg_len) != r) {
    return JS_ERROR_INTERNAL;
  }

  return r;
}

int ScanHead::TCPRead(uint8_t *buf, uint32_t len, uint32_t *unread_len, SOCKET fd)
{
  uint32_t msg_len = *unread_len;
  int r = 0;

  if (0 == msg_len) {
    // NOTE: receiving little-endian as to keep with approach used by
    // Flatbuffers
    r = recv(fd, reinterpret_cast<char *>(&msg_len), sizeof(uint32_t), 0);
    if (sizeof(uint32_t) != r) {
      return JS_ERROR_INTERNAL;
    }
    assert(msg_len < len);
    *unread_len = msg_len;
  }

  r = recv(fd, reinterpret_cast<char *>(buf), msg_len, 0);
  if (0 > r) {
    return JS_ERROR_INTERNAL;
  }

  *unread_len = msg_len - r;

  return r;
}

jsCamera ScanHead::CameraPortToId(uint32_t port)
{
  if (port >= m_spec.camera_port_to_id.size()) {
    return JS_CAMERA_INVALID;
  }

  return (jsCamera)m_spec.camera_port_to_id[port];
}

int32_t ScanHead::CameraIdToPort(jsCamera camera)
{
  uint32_t id = (uint32_t)camera;
  auto it = std::find(m_spec.camera_port_to_id.begin(),
                      m_spec.camera_port_to_id.end(), id);
  if (it == m_spec.camera_port_to_id.end()) {
    return -1;
  }

  // assumes that the position in the array indicates the port
  return it - m_spec.camera_port_to_id.begin();
}

jsLaser ScanHead::LaserPortToId(uint32_t port)
{
  if (port >= m_spec.laser_port_to_id.size()) {
    return JS_LASER_INVALID;
  }

  return (jsLaser)m_spec.laser_port_to_id[port];
}

int32_t ScanHead::LaserIdToPort(jsLaser laser)
{
  auto it = std::find(m_spec.laser_port_to_id.begin(),
                      m_spec.laser_port_to_id.end(), laser);
  if (it == m_spec.laser_port_to_id.end()) {
    return -1;
  }

  // assumes that the position in the array indicates the port
  return it - m_spec.laser_port_to_id.begin();
}
