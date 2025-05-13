/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_STATUS_MESSAGE_H
#define JOESCAN_STATUS_MESSAGE_H

#include "joescan_pinchot.h"

namespace joescan {

struct StatusMessage {
  jsScanHeadStatus user;
  uint32_t scan_head_ip;
  uint32_t client_ip;
  uint32_t client_port;
  uint32_t min_scan_period_us;

  StatusMessage(): scan_head_ip(0), client_ip(0), client_port(0), min_scan_period_us(0) {}
};

} // namespace joescan

#endif // JOESCAN_STATUS_MESSAGE_H
