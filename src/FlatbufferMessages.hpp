#ifndef _JOESCAN_FLATBUFFER_MESSAGES
#define _JOESCAN_FLATBUFFER_MESSAGES

#ifndef __linux__
// Suppress compiler warnings from Flatbuffer messages. We will trust that the
// code that is generated by the Flatbuffer compiler is okay.
#pragma warning(push, 0)
#endif

#include "MessageDiscoveryClient_generated.h"
#include "MessageDiscoveryServer_generated.h"
#include "MessageClient_generated.h"
#include "MessageServer_generated.h"
#include "MessageUpdateClient_generated.h"
#include "MessageUpdateServer_generated.h"
#include "ScanHeadType_generated.h"

#ifndef __linux__
#pragma warning(pop)
#endif

#endif
