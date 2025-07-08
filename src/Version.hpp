/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_VERSION_H
#define JOESCAN_VERSION_H

#include <cstdint>

#ifdef major
#undef major
#endif

#ifdef minor
#undef minor
#endif

namespace joescan {

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

/// The API version. Note that this must be manually incremented for each new
/// release; our build system does *not* manage this.
#define API_VERSION_MAJOR 16
#define API_VERSION_MINOR 3
#define API_VERSION_PATCH 1

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

  SemanticVersion(uint32_t version_major,
                  uint32_t version_minor,
                  uint32_t version_patch) :
                  major(version_major),
                  minor(version_minor),
                  patch(version_patch) { }

  SemanticVersion() : major(0), minor(0), patch(0) { }

  /// Test if `SemanticVersion` is greater than function argument.
  bool IsGreaterThan(uint32_t version_target_major,
                     uint32_t version_target_minor,
                     uint32_t version_target_patch)
  {
    if (version_target_major > this->major) {
      return false;
    } else if (version_target_major < this->major) {
      return true;
    }

    // major versions are equal

    if (version_target_minor > this->minor) {
      return false;
    } else if (version_target_minor < this->minor) {
      return true;
    }

    // minor versions are equal

    if (version_target_patch >= this->patch) { // Note: >=
      return false;
    }

    return true;
  }

  /// Test if `SemanticVersion` is less than function argument.
  bool IsLessThan(uint32_t version_target_major,
                  uint32_t version_target_minor,
                  uint32_t version_target_patch)
  {
    if (version_target_major < this->major) {
      return false;
    } else if (version_target_major > this->major) {
      return true;
    }

    // major versions are equal

    if (version_target_minor < this->minor) {
      return false;
    } else if (version_target_minor > this->minor) {
      return true;
    }

    // minor versions are equal

    if (version_target_patch <= this->patch) { // Note: <=
      return false;
    }

    return true;
  }

  /// Test if `SemanticVersion` is compatible with function argument.
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

  /// Test if `SemanticVersion` is greater than function argument.
  bool IsGreaterThan(SemanticVersion version)
  {
    return IsGreaterThan(version.major, version.minor, version.patch);
  }

  /// Test if `SemanticVersion` is less than function argument.
  bool IsLessThan(SemanticVersion version)
  {
    return IsLessThan(version.major, version.minor, version.patch);
  }

  /// Test if `SemanticVersion` is compatible with function argument.
  bool IsCompatible(SemanticVersion version)
  {
    return IsCompatible(version.major, version.minor, version.patch);
  }
};

} // namespace joescan

#endif
