/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

// TODO: replace strerror with strerror_s
#define _CRT_SECURE_NO_WARNINGS

#include "ScanHead.hpp"
#include "CameraLaserIterator.hpp"
#include "DataPacket.hpp"
#include "FlatbufferMessages.hpp"
#include "NetworkInterface.hpp"
#include "NetworkTypes.hpp"
#include "RawProfileToProfile.hpp"
#include "js50_spec_bin.h"

using namespace joescan;

#define CAMERA_GET_LASER(camera, laser) \
  if (m_model.IsLaserPrimary()) { \
    return JS_ERROR_USE_LASER_FUNCTION; \
  } \
  (laser) = m_model.GetPairedLaser((camera)); \
  if (JS_LASER_INVALID == (laser)) { \
    return JS_ERROR_INVALID_ARGUMENT; \
  }

#define LASER_GET_CAMERA(laser, camera) \
  if (m_model.IsCameraPrimary()) { \
    return JS_ERROR_USE_CAMERA_FUNCTION; \
  } \
  (camera) = m_model.GetPairedCamera((laser)); \
  if (JS_CAMERA_INVALID == (camera)) { \
    return JS_ERROR_INVALID_ARGUMENT; \
  }

ScanHead::ScanHead(ScanManager &manager, jsDiscovered &discovered, uint32_t id)
  : m_scan_manager(manager),
    m_model(discovered.type, discovered.serial_number, id),
    m_profiles(m_model),
    m_format(JS_DATA_FORMAT_XY_BRIGHTNESS_FULL),
    m_sock_ctrl(nullptr),
    m_builder(512),
    m_firmware_version{ discovered.firmware_version_major,
                        discovered.firmware_version_minor,
                        discovered.firmware_version_patch },
    m_ip_address(discovered.ip_addr),
    m_client_name(discovered.client_name_str),
    m_client_ip_address(discovered.client_ip_addr),
    m_min_ecoder_travel(0),
    m_idle_scan_period_ns(0),
    m_last_encoder(0),
    m_last_timestamp(0),
    m_last_sequence(0),
    m_is_receive_thread_active(false),
    m_is_frame_scanning(false),
    m_is_scanning(false)
{
  m_packet_buf = new uint8_t[kMaxPacketSize];
  m_packet_buf_len = kMaxPacketSize;

  auto units = m_scan_manager.GetUnits();
  m_data = std::unique_ptr<DynamicData>(new DynamicData(m_model, units));
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

int ScanHead::Connect(uint32_t timeout_s)
{
  using namespace schema::client;
  SOCKET fd = -1;
  int r = 0;

  m_mutex.lock();
  m_sock_ctrl = std::unique_ptr<TCPSocket>(
                  new TCPSocket(m_client_name, m_client_ip_address,
                                m_ip_address, kScanServerCtrlPort, timeout_s));

  m_builder.Clear();

  std::string api_version_string = API_VERSION_FULL;
  auto notes_offset = m_builder.CreateVectorOfStrings(
    {std::string("C API"), api_version_string});

  auto data_offset =
    CreateConnectData(m_builder,
                      m_model.GetSerialNumber(),
                      m_model.GetId(),
                      ConnectionType_NORMAL,
                      notes_offset);
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_CONNECT, MessageData_ConnectData,
                        data_offset.Union());
  m_builder.Finish(msg_offset);

  r = m_sock_ctrl->Send(m_builder);
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

  if (0 == r) {
    m_mutex.lock();
    m_is_receive_thread_active = true;
    std::thread receive_thread(&ScanHead::ThreadScanningReceive, this);
    m_receive_thread = std::move(receive_thread);
    m_mutex.unlock();
  }

  return 0;
}

int ScanHead::Disconnect(void)
{
  using namespace schema::client;

  m_mutex.lock();

  m_is_receive_thread_active = false;
  m_receive_thread.join();

  m_builder.Clear();
  auto msg_offset =
    CreateMessageClient(m_builder, MessageType_DISCONNECT, MessageData_NONE);
  m_builder.Finish(msg_offset);
  int r = m_sock_ctrl->Send(m_builder);
  m_sock_ctrl->Close();
  m_mutex.unlock();

  return r;
}

int ScanHead::SendScanConfiguration(uint32_t period_us, jsDataFormat fmt,
                                    bool is_frame_scanning)
{
  using namespace schema::client;
  std::unique_lock<std::mutex> lock(m_mutex);

  if (0 == m_scan_pairs.size()) {
    // TODO: Do we return error? Or do we just silently let it fail?
    return 0;
  }

  if ((period_us > m_model.GetMaxScanPeriod()) ||
      (period_us < m_model.GetMinScanPeriod())) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (is_frame_scanning && !m_firmware_version.IsCompatible(16, 2, 0)) {
    return JS_ERROR_VERSION_COMPATIBILITY;
  }

  uint32_t data_type_mask = 0;
  uint32_t data_stride = 0;
  switch (fmt) {
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_FULL):
      data_type_mask = uint32_t(DataType::XYData | DataType::Brightness);
      data_stride = 1;
      break;
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_HALF):
      data_type_mask = uint32_t(DataType::XYData | DataType::Brightness);
      data_stride = 2;
      break;
    case (JS_DATA_FORMAT_XY_BRIGHTNESS_QUARTER):
      data_type_mask = uint32_t(DataType::XYData | DataType::Brightness);
      data_stride = 4;
      break;
    case (JS_DATA_FORMAT_XY_FULL):
      data_type_mask = uint32_t(DataType::XYData);
      data_stride = 1;
      break;
    case (JS_DATA_FORMAT_XY_HALF):
      data_type_mask = uint32_t(DataType::XYData);
      data_stride = 2;
      break;
    case (JS_DATA_FORMAT_XY_QUARTER):
      data_type_mask = uint32_t(DataType::XYData);
      data_stride = 4;
      break;
    default:
      return JS_ERROR_INVALID_ARGUMENT;
  }

  const jsScanHeadConfiguration *config = m_data->GetConfiguration();
  ScanConfigurationDataT cfg;
  cfg.data_type_mask = data_type_mask;
  cfg.data_stride = data_stride;
  cfg.scan_period_ns = static_cast<uint32_t>(period_us * 1000);
  cfg.laser_detection_threshold = config->laser_detection_threshold;
  cfg.saturation_threshold = config->saturation_threshold;
  cfg.saturation_percent = config->saturation_percentage;


  for (auto &el : m_scan_pairs) {
    jsCamera camera = el.camera;
    jsLaser laser = el.laser;
    jsCableOrientation cable = m_data->GetAlignment(camera, laser)->cable;

    std::unique_ptr<CameraLaserConfigurationT> c(new CameraLaserConfigurationT);
    c->camera_port = m_model.CameraIdToPort(camera);
    c->laser_port = m_model.LaserIdToPort(laser);
    c->laser_on_time_min_ns = el.config.laser_on_time_min_us * 1000;
    c->laser_on_time_def_ns = el.config.laser_on_time_def_us * 1000;
    c->laser_on_time_max_ns = el.config.laser_on_time_max_us * 1000;
    c->scan_end_offset_ns = el.end_offset_us * 1000;

    if (m_model.GetCameraPortCableUpstream() == c->camera_port) {
      c->camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == cable) ?
                              CameraOrientation_UPSTREAM :
                              CameraOrientation_DOWNSTREAM;
    } else {
      c->camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == cable) ?
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
  int r = m_sock_ctrl->Send(m_builder);

  m_format = fmt;

  return r;
}

int ScanHead::SendScanAlignmentValue()
{
  using namespace schema::client;
  std::unique_lock<std::mutex> lock(m_mutex);
  int r = 0;

  if (0 == m_scan_pairs.size()) {
    // TODO: Do we return error? Or do we just silently let it fail?
    return 0;
  }

  for (auto &el : m_scan_pairs) {
    jsCamera camera = el.camera;
    jsLaser laser = el.laser;
    const Alignment *alignment = m_data->GetAlignment(camera, laser);

    if ((0 == alignment->shift_x) &&
        (0 == alignment->shift_y) &&
        (0 == alignment->roll)) {
      continue;
    }

    StoreAlignmentDataT alignment_data;
    alignment_data.camera_port = m_model.CameraIdToPort(camera);
    alignment_data.laser_port = m_model.LaserIdToPort(laser);
    alignment_data.x_offset = alignment->shift_x;
    alignment_data.y_offset = alignment->shift_y;
    alignment_data.roll = alignment->roll;
    //TODO: Add fit error - how is this obtained?
    alignment_data.fit_error = 0.0;

    int64_t timestamp = 
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::string api_version_string = API_VERSION_FULL;

    m_builder.Clear();
    auto notes_offset = 
      m_builder.CreateVectorOfStrings(
        {std::string("C API"), api_version_string});

    auto alignment_data_offset =
      StoreAlignmentData::Pack(m_builder, &alignment_data);
    auto store_info_data_offset =
      CreateStoreInfoData(m_builder, timestamp, notes_offset,
                          StoreType_ALIGNMENT, StoreData_StoreAlignmentData,
                          alignment_data_offset.Union());

    auto msg_offset =
      CreateMessageClient(m_builder, MessageType_STORE_INFO,
                          MessageData_StoreInfoData,
                          store_info_data_offset.Union());

    m_builder.Finish(msg_offset);
    r = m_sock_ctrl->Send(m_builder);
    if (0 != r) {
      return r;
    }
  }

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
  int r = m_sock_ctrl->Send(m_builder);

  return r;
}

int ScanHead::StartScanning(uint64_t start_time_ns, bool is_frame_scanning)
{
  using namespace schema::client;

  std::unique_lock<std::mutex> lock(m_mutex);

  if (is_frame_scanning) {
    m_profiles.Reset(ProfileQueue::MODE_MULTI);
  } else {
    m_profiles.Reset(ProfileQueue::MODE_SINGLE);
  }

  m_builder.Clear();
  if (0 != start_time_ns) {
    // API commands time to start
    auto data_offset = CreateScanStartData(m_builder, start_time_ns);
    auto msg_offset = CreateMessageClient(m_builder, MessageType_SCAN_START,
                                          MessageData_ScanStartData,
                                          data_offset.Union());
    m_builder.Finish(msg_offset);
  } else {
    // Leave start time to determination of scan head
    auto msg_offset = CreateMessageClient(m_builder, MessageType_SCAN_START,
                                          MessageData_NONE);
    m_builder.Finish(msg_offset);
  }
  int r = m_sock_ctrl->Send(m_builder);
  if (0 == r) {
    m_is_frame_scanning = is_frame_scanning;
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
  int r = m_sock_ctrl->Send(m_builder);

  if (0 == r) {
    m_is_scanning = false;
  }

  return r;
}

int ScanHead::SendBrightnessCorrection()
{
  int r = 0;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = SendBrightnessCorrection(pair.first, pair.second);
  }

  return r;
}

int ScanHead::SendExclusionMask()
{
  int r = 0;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = SendExclusionMask(pair.first, pair.second);
  }

  return r;
}

int ScanHead::SendWindow()
{
  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    jsCamera camera = pair.first;
    jsLaser laser = pair.second;

    int r = SendWindow(camera, laser);
    if (0 != r) {
      return r;
    }
  }

  return 0;
}

bool ScanHead::IsConnected()
{
  // TODO: fix, grab status message
  if (nullptr == m_sock_ctrl) {
    return false;
  }

  return m_sock_ctrl->IsOpen();
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

  int32_t tmp = m_model.CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = m_model.LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  // NOTE: Generating an image invalidates window data on the scan head; we
  // will need to resend this data to the scan head before scanning if it has
  // already been sent.
  m_data->SetDirty();

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
    int r = m_sock_ctrl->Send(m_builder);
    if (0 > r) {
      return r;
    }
  }

  {
    using namespace schema::server;
    // size of buffer was determined by measuring the size of flatbuffer
    // message returning the image data; if the size of the message increases
    // during development, you should see an `assert` message failure in
    // TCP read where the framing message word indicating the message's size is
    // greater than the buffer length available to read the message into
    uint32_t buf_len = 0x200000;
    std::vector<uint8_t> buf(buf_len, 0);

    int r = m_sock_ctrl->Read(&buf[0], buf_len);
    if ((0 > r) || (0 == r)) {
      return JS_ERROR_NETWORK;
    }

    auto verifier = flatbuffers::Verifier(&buf[0], r);
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

    image->scan_head_id = m_model.GetId();
    image->timestamp_ns = data->timestamp_ns();
    image->camera = m_model.CameraPortToId(data->camera_port());
    image->laser = m_model.LaserPortToId(data->laser_port());
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

  int32_t tmp = m_model.CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = m_model.LaserIdToPort(laser);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t laser_port = (uint32_t) tmp;

  {
    using namespace schema::client;

    const jsScanHeadConfiguration *config = m_data->GetConfiguration();
    jsCableOrientation cable = m_data->GetAlignment(camera, laser)->cable;

    ProfileRequestDataT data;
    data.camera_port = camera_port;
    data.laser_port = laser_port;
    data.camera_exposure_ns = camera_exposure_us * 1000;
    data.laser_on_time_ns = laser_on_time_us * 1000;
    data.laser_detection_threshold = config->laser_detection_threshold;
    data.saturation_threshold = config->saturation_threshold;

    if (m_model.GetCameraPortCableUpstream() == camera_port) {
      data.camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == cable) ?
                                CameraOrientation_UPSTREAM :
                                CameraOrientation_DOWNSTREAM;
    } else {
      data.camera_orientation = (JS_CABLE_ORIENTATION_UPSTREAM == cable) ?
                                CameraOrientation_DOWNSTREAM :
                                CameraOrientation_UPSTREAM;
    }

    m_builder.Clear();

    auto data_offset = ProfileRequestData::Pack(m_builder, &data);
    auto msg_offset =
      CreateMessageClient(m_builder, MessageType_PROFILE_REQUEST,
                          MessageData_ProfileRequestData, data_offset.Union());
    m_builder.Finish(msg_offset);
    int r = m_sock_ctrl->Send(m_builder);
    if(0 > r) {
      return r;
    }
  }

  {
    using namespace schema::server;

    uint32_t buf_len = 0x8000;
    std::vector<uint8_t> buf(buf_len, 0);

    int r = m_sock_ctrl->Read(&buf[0], buf_len);
    if ((0 > r) || (0 == r)) {
      return JS_ERROR_NETWORK;
    }

    auto verifier = flatbuffers::Verifier(&buf[0], r);
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

    profile->scan_head_id = m_model.GetId();
    profile->timestamp_ns = data->timestamp_ns();
    profile->camera = m_model.CameraPortToId(data->camera_port());
    profile->laser = m_model.LaserPortToId(data->laser_port());
    profile->laser_on_time_us = data->laser_on_time_ns() / 1000;
    profile->num_encoder_values = encoders_size;
    profile->packets_received = 0;
    profile->packets_expected = 0;
    profile->data_len = JS_RAW_PROFILE_DATA_LEN;
    profile->data_valid_brightness = data->valid_points();
    profile->data_valid_xy = data->valid_points();

    auto *transform = m_data->GetTransform(profile->camera, profile->laser);
    const int16_t INVALID_XY = -32768;

    for (uint32_t n = 0; n < points->size(); n++) {
      auto point = points->Get(n);
      int16_t x_raw = point->x();
      int16_t y_raw = point->y();
      uint16_t brightness = point->brightness();

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);

        Point2D<int32_t> p = transform->CameraToMill(x, y);
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

uint32_t ScanHead::WaitUntilAvailableProfiles(uint32_t count,
                                              uint32_t timeout_us)
{
  // std::condition_variable::wait
  // Atomically unlocks lock, blocks the current executing thread, and adds it
  // to the list of threads waiting on *this. The thread will be unblocked when
  // notify_all() or notify_one() is executed. It may also be unblocked
  // spuriously. When unblocked, regardless of the reason, lock is reacquired
  // and wait exits.
  if (m_is_scanning) {
    std::chrono::duration<uint32_t, std::micro> timeout(timeout_us);
    std::unique_lock<std::mutex> lock(m_new_data_mtx);
    m_new_data_cv.wait_for(
      lock, timeout, [this, count] {
        return ((m_profiles.SizeReady() >= count) || (!m_is_scanning));
      }
    );
  }

  return static_cast<uint32_t>(m_profiles.SizeReady());
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

    r = m_sock_ctrl->Send(m_builder);
    if (0 != r) {
      return r;
    }

    r = m_sock_ctrl->Read(&buf[0], buf_len);
    if ((0 > r) || (0 == r)) {
      return JS_ERROR_NETWORK;
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
      jsCamera camera = m_model.CameraPortToId(c->port);
      if (JS_CAMERA_A == camera) {
        m_status.user.camera_a_pixels_in_window = c->pixels_in_window;
        m_status.user.camera_a_temp = c->temperature;
      } else if (JS_CAMERA_B == camera) {
        m_status.user.camera_b_pixels_in_window = c->pixels_in_window;
        m_status.user.camera_b_temp = c->temperature;
      }
    }

    m_status.user.num_encoder_values = (uint32_t) data->encoders.size();
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

ProfileQueue *ScanHead::GetProfileQueue()
{
  return &m_profiles;
}

bool ScanHead::IsConfigurationValid(jsScanHeadConfiguration &cfg)
{
  return m_model.IsConfigurationValid(cfg);
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

  return m_data->SetConfiguration(cfg);
}

uint32_t ScanHead::GetMinScanPeriod()
{
  uint32_t s1 = m_status.min_scan_period_us;
  uint32_t s2 = m_model.GetMinScanPeriod();
  return (s1 < s2) ? s2 : s1;
}

void ScanHead::ResetScanPairs()
{
  m_scan_pairs.clear();
}

int ScanHead::AddScanPair(jsCamera camera, jsLaser laser,
                          jsScanHeadConfiguration &cfg, uint32_t end_offset_us)
{
  if (false == m_model.IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (!(IsConfigurationValid(cfg))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (m_scan_pairs.size() >= m_model.GetMaxConfigurationGroups()) {
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

int ScanHead::SetAlignment(double roll_degrees, double shift_x, double shift_y)
{
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = m_data->SetAlignment(pair.first,
                             pair.second,
                             roll_degrees,
                             shift_x,
                             shift_y);
    if (0 != r) {
      return r;
    }
  }

  return r;
}

int ScanHead::SetAlignment(jsCamera camera, double roll_degrees, double shift_x,
                           double shift_y)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return m_data->SetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::SetAlignment(jsLaser laser, double roll_degrees, double shift_x,
                           double shift_y)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return m_data->SetAlignment(camera, laser, roll_degrees, shift_x, shift_y);
}

int ScanHead::GetAlignment(jsCamera camera, double *roll_degrees,
                           double *shift_x, double *shift_y)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  auto a = m_data->GetAlignment(camera, laser);
  *roll_degrees = a->roll;
  *shift_x = a->shift_x;
  *shift_y = a->shift_y;

  return 0;
}

int ScanHead::GetAlignment(jsLaser laser, double *roll_degrees, double *shift_x,
                           double *shift_y)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  auto a = m_data->GetAlignment(camera, laser);
  *roll_degrees = a->roll;
  *shift_x = a->shift_x;
  *shift_y = a->shift_y;

  return 0;
}

int ScanHead::SetExclusionMask(jsExclusionMask *mask)
{
  int r = 0;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = m_data->SetExclusionMask(pair.first, pair.second, *mask);
  }

  return r;
}

int ScanHead::SetExclusionMask(jsCamera camera, jsExclusionMask *mask)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return m_data->SetExclusionMask(camera, laser, *mask);
}

int ScanHead::SetExclusionMask(jsLaser laser, jsExclusionMask *mask)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return m_data->SetExclusionMask(camera, laser, *mask);
}

int ScanHead::GetExclusionMask(jsCamera camera, jsExclusionMask *mask)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  auto m = m_data->GetExclusionMask(camera, laser);
  memcpy(mask, m, sizeof(jsExclusionMask));

  return 0;
}

int ScanHead::GetExclusionMask(jsLaser laser, jsExclusionMask *mask)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  auto m = m_data->GetExclusionMask(camera, laser);
  memcpy(mask, m, sizeof(jsExclusionMask));

  return 0;
}

int ScanHead::SetBrightnessCorrection(jsCamera camera,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return m_data->SetBrightnessCorrection(camera, laser, *correction);
}

int ScanHead::SetBrightnessCorrection(jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return m_data->SetBrightnessCorrection(camera, laser, *correction);
}

int ScanHead::GetBrightnessCorrection(jsCamera camera,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  auto c = m_data->GetBrightnessCorrection(camera, laser);
  memcpy(correction, c, sizeof(jsBrightnessCorrection_BETA));

  return 0;
}

int ScanHead::GetBrightnessCorrection(jsLaser laser,
                                      jsBrightnessCorrection_BETA *correction)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  auto c = m_data->GetBrightnessCorrection(camera, laser);
  memcpy(correction, c, sizeof(jsBrightnessCorrection_BETA));

  return 0;
}

int ScanHead::SetWindow(ScanWindow &window)
{
  using namespace schema::client;
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = m_data->SetWindow(pair.first, pair.second, window);
  }

  return r;
}

int ScanHead::SetWindow(jsCamera camera, ScanWindow &window)
{
  jsLaser laser;

  CAMERA_GET_LASER(camera, laser);

  return m_data->SetWindow(camera, laser, window);
}

int ScanHead::SetWindow(jsLaser laser, ScanWindow &window)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return m_data->SetWindow(camera, laser, window);
}

int ScanHead::SetPolygonWindow(jsCoordinate *points, uint32_t points_len)
{
  // set to internal error in case loop doesn't run due to invalid specification
  int r = JS_ERROR_INTERNAL;

  auto iter = CameraLaserIterator(m_model);
  for (auto &pair : iter) {
    r = m_data->SetPolygonWindow(pair.first, pair.second, points, points_len);
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

  return m_data->SetPolygonWindow(camera, laser, points, points_len);
}

int ScanHead::SetPolygonWindow(jsLaser laser, jsCoordinate *points,
                     uint32_t points_len)
{
  jsCamera camera;

  LASER_GET_CAMERA(laser, camera);

  return m_data->SetPolygonWindow(camera, laser, points, points_len);
}

int ScanHead::SendExclusionMask(jsCamera camera, jsLaser laser)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    // scan head doesn't support, don't send
    return 0;
  }

  if (!m_model.IsPairValid(camera, laser)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  auto mask = m_data->GetExclusionMask(camera, laser);

  int32_t tmp = m_model.CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = m_model.LaserIdToPort(laser);
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
  int r = m_sock_ctrl->Send(m_builder);
  if(0 > r) {
    return r;
  }

  return 0;
}

int ScanHead::SendBrightnessCorrection(jsCamera camera, jsLaser laser)
{
  if (!m_firmware_version.IsCompatible(16, 1, 0)) {
    // scan head doesn't support, don't send
    return 0;
  }

  if ((false == m_model.IsCameraValid(camera)) ||
      (false == m_model.IsLaserValid(laser))) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  auto corr = m_data->GetBrightnessCorrection(camera, laser);

  int32_t tmp = m_model.CameraIdToPort(camera);
  if (0 > tmp) {
    return JS_ERROR_INVALID_ARGUMENT;
  }
  uint32_t camera_port = (uint32_t) tmp;

  tmp = m_model.LaserIdToPort(laser);
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
  int r = m_sock_ctrl->Send(m_builder);
  if(0 > r) {
    return r;
  }

  return 0;
}

int ScanHead::SendWindow(jsCamera camera, jsLaser laser)
{
  using namespace schema::client;

  std::unique_lock<std::mutex> lock(m_mutex);
  WindowConfigurationDataT data;
  int port = 0;
  int r = 0;

  port = m_model.CameraIdToPort(camera);
  if (-1 == port) {
    return JS_ERROR_INTERNAL;
  }
  data.camera_port = port;

  port = m_model.LaserIdToPort(laser);
  if (-1 == port) {
    return JS_ERROR_INTERNAL;
  }
  data.laser_port = port;

  auto pair = std::make_pair(camera, laser);
  auto alignment = m_data->GetAlignment(camera, laser);
  auto transform = m_data->GetTransform(camera, laser);
  auto window = m_data->GetWindow(camera, laser);

  std::vector<WindowConstraint> constraints = window->GetConstraints();
  for (auto const &c : constraints) {
    Point2D<int32_t> p0, p1;
    std::unique_ptr<ConstraintT> constraint(new ConstraintT);

    // Note: units are in 1/1000 inch
    // calculate the first point of our window constraint
    p0.x = static_cast<int32_t>(c.constraints[0].x);
    p0.y = static_cast<int32_t>(c.constraints[0].y);
    // convert the point to the camera's coordinate system
    p0 = transform->MillToCamera(p0.x, p0.y);
    // calculate the second point of out window constraint
    p1.x = static_cast<int32_t>(c.constraints[1].x);
    p1.y = static_cast<int32_t>(c.constraints[1].y);
    // convert the point to the camera's coordinate system
    p1 = transform->MillToCamera(p1.x, p1.y);
    // pass constraint points to message to create the constraint
    if (JS_CABLE_ORIENTATION_DOWNSTREAM == alignment->cable) {
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
  r = m_sock_ctrl->Send(m_builder);
  if (0 != r) {
    return r;
  }

  return 0;
}

int ScanHead::ProcessProfile(DataPacket *packet, jsRawProfile *raw)
{
  const jsCamera camera = m_model.CameraPortToId(packet->header.camera_port);
  const jsLaser laser = m_model.LaserPortToId(packet->header.laser_port);

  raw->scan_head_id = packet->header.scan_head_id;
  raw->camera = camera;
  raw->laser = laser;
  raw->timestamp_ns = packet->header.timestamp_ns;
  raw->flags = packet->header.flags;
  raw->sequence_number = packet->header.sequence_number;
  raw->laser_on_time_us = packet->header.laser_on_time_us;
  raw->format = m_format;
  raw->data_len = JS_RAW_PROFILE_DATA_LEN;
  raw->data_valid_brightness = 0;
  raw->data_valid_xy = 0;
  raw->num_encoder_values = 0;

  // TODO: eventually deprecate
  raw->packets_expected = 1;
  raw->packets_received = 1;

  assert(packet->encoders.size() < JS_ENCODER_MAX);
  for (uint32_t n = 0; n < packet->encoders.size(); n++) {
    raw->encoder_values[n] = packet->encoders[n];
    raw->num_encoder_values++;
  }

  for (uint32_t n = 0; n < JS_RAW_PROFILE_DATA_LEN; n++) {
    raw->data[n].x = JS_PROFILE_DATA_INVALID_XY;
    raw->data[n].y = JS_PROFILE_DATA_INVALID_XY;
    raw->data[n].brightness = JS_PROFILE_DATA_INVALID_BRIGHTNESS;
  }

  // server sends int16_t x/y data points; invalid is int16_t minimum
  const int16_t INVALID_XY = -32768;
  auto transform = m_data->GetTransform(camera, laser);

  // if Brightness, assume X/Y data is present
  if (packet->data_brightness) {
    uint8_t *b_src = packet->data_brightness;
    int16_t *xy_src = packet->data_xy;

    // assume num_vals is same for both layouts
    for (uint32_t n = 0; n < packet->data_count; n++) {
      int16_t x_raw = htons(*xy_src++);
      int16_t y_raw = htons(*xy_src++);
      uint8_t brightness = *b_src++;

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);
        uint32_t idx = n * packet->data_stride;

        Point2D<int32_t> point = transform->CameraToMill(x, y);
        raw->data[idx].x = point.x;
        raw->data[idx].y = point.y;
        raw->data[idx].brightness = brightness;
        raw->data_valid_xy++;
        raw->data_valid_brightness++;
      }
    }
  } else if (packet->data_xy) {
    int16_t *src = packet->data_xy;

    for (uint32_t n = 0; n < packet->data_count; n++) {
      int16_t x_raw = htons(*src++);
      int16_t y_raw = htons(*src++);

      if ((INVALID_XY != x_raw) && (INVALID_XY != y_raw)) {
        int32_t x = static_cast<int32_t>(x_raw);
        int32_t y = static_cast<int32_t>(y_raw);
        uint32_t idx = n * packet->data_stride;

        Point2D<int32_t> point = transform->CameraToMill(x, y);
        raw->data[idx].x = point.x;
        raw->data[idx].y = point.y;
        raw->data_valid_xy++;
      }
    }
  }
#if 0
  // we don't support this
  else if (packet->data_subpixel) {
    uint16_t *src = data_subpixel;

    for (uint32_t n = 0; n < packet->data_count; n++) {
      // insert subpixel
    }
  }
#endif
  return 0;
}

void ScanHead::ThreadScanningReceive()
{
#ifdef _WIN32
  // Bump up thread priority; receiving profiles is the most important thing
  // for end users.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

  auto sock = TCPSocket(m_client_name, m_client_ip_address, m_ip_address,
                        kScanServerDataPort);

  while (m_is_receive_thread_active) {
    uint8_t *buf = m_packet_buf;
    uint32_t buf_len = m_packet_buf_len;
    int r = 0;

    r = sock.Read(buf, buf_len, &m_is_receive_thread_active);
    if ((0 >= r) || (!m_is_receive_thread_active)) {
      // Connection closed or commanded to stop; stop the thread.
      return;
    }

    const uint16_t magic = (buf[0] << 8) | (buf[1]);
    if (kDataMagic != magic) {
      // Not a profile? What could this be?
      continue;
    }

    // Deserialize data
    DataPacket packet = DataPacket(buf);
    const jsCamera camera = m_model.CameraPortToId(packet.header.camera_port);
    const jsLaser laser = m_model.LaserPortToId(packet.header.laser_port);

    if (m_is_frame_scanning) {
      // Only process profile data if there is free memory available that can be
      // used to hold new profile data. If no free memory, skip processing; in
      // effect, dropping the profile in software.
      r = m_profiles.SizeFree(camera, laser);
      if (0 == r) {
        // User stopped reading out profiles; no free memory available.
        continue;
      }

      jsRawProfile *raw = nullptr;
      r = m_profiles.DequeueFree(camera, laser, &raw);
      // Should not fail having checked the size previously...
      assert(0 == r);

      ProcessProfile(&packet, raw);

      // Send new profile over to queue for user to read out.
      r = m_profiles.EnqueueReady(camera, laser, &raw);
      // should not fail
      assert(0 == r);
      m_last_sequence = packet.header.sequence_number;
    } else {
      if ((0 < m_min_ecoder_travel) && (0 < packet.encoders.size())) {
        uint32_t t = (uint32_t) std::abs(packet.encoders[0] - m_last_encoder);
        uint32_t d = (uint32_t) (packet.header.timestamp_ns - m_last_timestamp);
        if (t < m_min_ecoder_travel) {
          if (0 == m_idle_scan_period_ns) continue;
          if (d < m_idle_scan_period_ns) continue;
        }

        m_last_encoder = packet.encoders[0];
        m_last_timestamp = packet.header.timestamp_ns;
        m_last_sequence = packet.header.sequence_number;
      }

      // Only process profile data if there is free memory available that can be
      // used to hold new profile data. If no free memory, skip processing; in
      // effect, dropping the profile in software.
      r = m_profiles.SizeFree();
      if (0 == r) {
        // User stopped reading out profiles; no free memory available.
        continue;
      }

      jsRawProfile *raw = nullptr;
      r = m_profiles.DequeueFree(&raw);
      // Should not fail having checked the size previously...
      assert(0 == r);

      ProcessProfile(&packet, raw);

      // Send new profile over to queue for user to read out.
      r = m_profiles.EnqueueReady(&raw);
      // should not fail
      assert(0 == r);
      m_last_sequence = packet.header.sequence_number;

      // std::condition_variable::notify_all:
      // The notifying thread does not need to hold the lock on the same mutex
      // as the one held by the waiting thread(s); in fact doing so is a
      // pessimization, since the notified thread would immediately block again,
      // waiting for the notifying thread to release the lock.
      m_new_data_cv.notify_all();
    }
  }

  // Final notify in case user is in `jsScanHeadWaitUntilProfilesAvailable()`.
  m_new_data_cv.notify_all();
}

/**
 * These functions are really a small shim layer. In the future, we want to
 * move all the code in this file to `joescan_api.cpp`; code below is easy
 * drop in.
 */

jsScanHeadType ScanHead::GetType() const
{
  return m_model.GetType();
}

uint32_t ScanHead::GetSerialNumber() const
{
  return m_model.GetSerialNumber();
}

uint32_t ScanHead::GetId() const
{
  return m_model.GetId();
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
  capabilities.max_camera_image_height = m_model.GetMaxCameraRows();
  capabilities.max_camera_image_width = m_model.GetMaxCameraColumns();
  capabilities.max_scan_period_us = m_model.GetMaxScanPeriod();
  capabilities.min_scan_period_us = m_model.GetMinScanPeriod();
  capabilities.num_cameras = m_model.GetNumberOfCameras();
  capabilities.num_encoders = 1; // hardcoded for now
  capabilities.num_lasers = m_model.GetNumberOfLasers();

  return capabilities;
}

bool ScanHead::IsScanning()
{
  return m_is_scanning;
}

jsCamera ScanHead::GetPairedCamera(jsLaser laser)
{
  return m_model.GetPairedCamera(laser);
}

jsLaser ScanHead::GetPairedLaser(jsCamera camera)
{
  return m_model.GetPairedLaser(camera);
}

uint32_t ScanHead::GetCameraLaserPairCount()
{
  return m_model.GetCameraLaserPairCount();
}

uint32_t ScanHead::AvailableProfiles()
{
  return static_cast<uint32_t>(m_profiles.SizeReady());
}

jsScanHeadConfiguration ScanHead::GetConfiguration() const
{
  jsScanHeadConfiguration cfg = *(m_data->GetConfiguration());
  return cfg;
}

jsScanHeadConfiguration ScanHead::GetConfigurationDefault() const
{
  jsScanHeadConfiguration cfg = *(m_data->GetDefaultConfiguration());
  return cfg;
}

uint32_t ScanHead::GetMaxScanPairs()
{
  return m_model.GetMaxConfigurationGroups();
}

int ScanHead::SetCableOrientation(jsCableOrientation cable)
{
  return m_data->SetCableOrientation(cable);
}

jsCableOrientation ScanHead::GetCableOrientation()
{
  return m_data->GetCableOrientation();
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
  return (uint32_t) (m_idle_scan_period_ns / 1000);
}

int32_t ScanHead::SetIdleScanPeriod(uint32_t period_us)
{
  if (m_is_scanning) {
    return JS_ERROR_SCANNING;
  }

  m_idle_scan_period_ns = period_us * 1000;
  return 0;
}

uint32_t ScanHead::GetLastSequenceNumber()
{
  return m_last_sequence;
}

bool ScanHead::IsDirty()
{
  return m_data->IsDirty();
}

void ScanHead::ClearDirty()
{
  m_data->ClearDirty();
}
