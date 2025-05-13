#ifndef _SCANSYNC_NETWORK_INCLUDES_H
#define _SCANSYNC_NETWORK_INCLUDES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SCANSYNC_UDP_PORT 11234

#define SCANSYNC_UDP_PACKET_VERSION 4

#define SCANSYNC_PACKET_V1_SIZE_BYTES 32
#define SCANSYNC_PACKET_V2_SIZE_BYTES 76
#define SCANSYNC_PACKET_V3_SIZE_BYTES 76
#define SCANSYNC_PACKET_V4_SIZE_BYTES 76
#define SCANSYNC_PACKET_MAX_SIZE_BYTES SCANSYNC_PACKET_V4_SIZE_BYTES

#define FLAG_BIT_MASK_FAULT_A 0x00000001
#define FLAG_BIT_MASK_FAULT_B 0x00000002
#define FLAG_BIT_MASK_FAULT_Y 0x00000004
#define FLAG_BIT_MASK_FAULT_Z 0x00000008
#define FLAG_BIT_MASK_OVERRUN 0x00000010
#define FLAG_BIT_MASK_TERMINATION_ENABLE 0x00000020
#define FLAG_BIT_MASK_INDEX_Z 0x00000040
#define FLAG_BIT_MASK_SYNC 0x00000080
#define FLAG_BIT_MASK_AUX_Y 0x00000100

// V4 flags
#define FLAG_BIT_MASK_FAULT_SYNC 0x00000200
#define FLAG_BIT_MASK_LASER_DISABLE 0x00000400
#define FLAG_BIT_MASK_FAULT_LASER_DISABLE 0x00000800

#pragma pack(push, 1)
struct scansync_udp_packet
{
    /// version 1 packet data
    uint32_t    serial_number;          // 0 byte offset
    uint32_t    sequence;               // 4
    uint32_t    encoder_timestamp_s;    // 8
    uint32_t    encoder_timestamp_ns;   // 12
    uint32_t    last_timestamp_s;       // 16
    uint32_t    last_timestamp_ns;      // 20
    int64_t     encoder;                // 24
    /// version 2 packet data
    uint32_t flags;                     // 32
    uint32_t aux_y_timestamp_s;         // 36
    uint32_t aux_y_timestamp_ns;        // 40
    uint32_t index_z_timestamp_s;       // 44
    uint32_t index_z_timestamp_ns;      // 48
    uint32_t sync_timestamp_s;          // 52
    uint32_t sync_timestamp_ns;         // 56
    /// version 3 use of previously reserved fields in v2
    uint16_t packet_version;            // 60
    uint16_t firmware_version_major;    // 62
    uint16_t firmware_version_minor;    // 64
    uint16_t firmware_version_patch;    // 66
    /// version 4 use of one of the previously reserved fields in v3
    uint32_t laser_disable_timestamp_s; // 68
    uint32_t laser_disable_timestamp_ns;// 72
};
#pragma pack(pop)

#ifdef __cplusplus
} // extern "C" {
#endif

#endif
