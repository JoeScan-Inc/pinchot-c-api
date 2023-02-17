/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_VERSION_H
#define JOESCAN_VERSION_H

#include <cstdint>

namespace joescan {

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define API_VERSION_MAJOR 16
#define API_VERSION_MINOR 1
#define API_VERSION_PATCH 3

#define API_VERSION_SEMANTIC \
  STR(API_VERSION_MAJOR) "." STR(API_VERSION_MINOR) "." STR(API_VERSION_PATCH)

#ifndef API_GIT_HASH
  #define API_GIT_HASH ""
#endif

#ifndef API_DIRTY_FLAG
  #define API_DIRTY_FLAG ""
#endif

#define API_VERSION_FULL API_VERSION_SEMANTIC API_DIRTY_FLAG API_GIT_HASH

struct SemanticVersion {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;

  bool IsCompatible(uint32_t version_target_major,
                    uint32_t version_target_minor,
                    uint32_t version_target_patch)
  {
  #ifdef NO_SCAN_HEAD_VERSION_CHECK
    return true;
  #endif

    if (version_target_major > this->major) {
      return false;
    } else if (version_target_major == this->major) {
      if (version_target_minor > this->minor) {
        return false;
      } else if (version_target_minor == this->minor) {
        if (version_target_patch > this->patch) {
          return false;
        }
      }
    }

    return true;
  }
};

} // namespace joescan

#endif
