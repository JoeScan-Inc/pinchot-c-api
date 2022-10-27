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
#define API_VERSION_MINOR 0
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


} // namespace joescan

#endif
