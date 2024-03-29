#ifndef _JOESCAN_FLATBUFFER_MESSAGES
#define _JOESCAN_FLATBUFFER_MESSAGES

#ifdef _WIN32
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

#ifdef _WIN32
#pragma warning(pop)
#endif

#endif
