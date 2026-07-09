/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_TCP_SERIALIZATION_HELPERS_H
#define JOESCAN_TCP_SERIALIZATION_HELPERS_H

#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace joescan {
// TODO: This is really only needed for 64-bit values, it's probably worth
// forcing non-64-bit values to be converted using ntoh/hton.
template <typename T>
T hostToNetwork(T value)
{
  // The number of bits per byte.
  static const int kBitsPerByte = 8;

  T oneEnd = 0xff;
  if (*(reinterpret_cast<uint8_t *>(&oneEnd)) != 0xff) {
    return value;
  }

  T swizzled = 0;
  for (unsigned int i = 0; i < sizeof(T); i++) {
    // extract the next byte from the original value
    uint8_t byte = (value >> (i * kBitsPerByte)) & 0xff;
    // calculate where to swizzle that byte to
    uint8_t finalPosition = static_cast<uint8_t>(sizeof(T) - (1 + i));
    // stuff that byte into the swizzled location
    swizzled |= static_cast<uint64_t>(byte) << (finalPosition * kBitsPerByte);
  }
  return swizzled;
}

static size_t SerializeBytesToCollection(std::vector<uint8_t> &serialized,
                                         size_t size, const uint8_t *ptr)
{
  // There must be a valid pointer if there is data to serialize, but it's
  // fine to not supply a valid pointer when there is no data.
  if (ptr == nullptr && size != 0) {
    throw std::exception();
  }

  for (unsigned int i = 0; i < size; i++) {
    serialized.push_back(ptr[i]);
  }

  return size;
}

template <typename T>
size_t SerializeIntegralToBytes(std::vector<uint8_t> &serialized, const T *ptr)
{
  T value = hostToNetwork<T>(*ptr);
  return SerializeBytesToCollection(serialized, sizeof(T),
                                    reinterpret_cast<const uint8_t *>(&value));
}

template <typename T>
T DeserializeIntegralFromBytes(std::vector<uint8_t> &serialized)
{
  if (serialized.size() < sizeof(T)) {
    throw std::exception();
  }

  T value = hostToNetwork(*(reinterpret_cast<T *>(serialized.data())));
  serialized.erase(serialized.begin(), serialized.begin() + sizeof(T));

  return value;
}

static size_t SerializeStringToBytes(std::vector<uint8_t> &serialized,
                                     const std::string &str)
{
  const uint8_t *bytePtr = reinterpret_cast<const uint8_t *>(str.data());
  // TODO: 8 byte length value is overkill, cast to 4?  If so, MUST change
  // the corresponding length in DeserializeStringFromBytes.
  size_t length, count;
  length = hostToNetwork(str.length());
  count = SerializeBytesToCollection(serialized, sizeof(length),
                                     reinterpret_cast<uint8_t *>(&length));

  count += SerializeBytesToCollection(serialized, str.length(), bytePtr);
  return count;
}

static std::string DeserializeStringFromBytes(std::vector<uint8_t> &serialized)
{
  // The serialized data better hold the string size, even if 0.
  if (serialized.size() < sizeof(size_t)) {
    throw std::exception();
  }

  // Get the string length & unswizzle it if necessary.
  size_t length =
    hostToNetwork(*(reinterpret_cast<size_t *>(serialized.data())));
  // The serialized data must hold enough characters for our string.
  if (serialized.size() < sizeof(size_t) + length) {
    throw std::exception();
  }

  // Copy the characters for the string into a new string, and then erase
  // them from the serialized data, along with the string length.
  const char *rawChars =
    reinterpret_cast<const char *>(serialized.data() + sizeof(size_t));
  std::string str(rawChars, length);
  serialized.erase(serialized.begin(),
                   serialized.begin() + sizeof(size_t) + length);

  return str;
}

#ifdef _MSC_VER
#pragma warning(push)
// C4244: 'argument': conversion from 'T' to 'u_long', possible loss of data
#pragma warning(disable : 4244)
#endif //_MSC_VER
/**
 * Interprets a number of bytes located in `buf` as the datatype of `data`
 * and sets `data` equal to the network-to-host translation of that
 *
 * Currently only works for 8/16/32/64 bit int/uint datatypes
 *
 * Returns the size (in bytes) of `data` to allow for easy chaining of
 * reads from the same buffer without having to manually do math, or
 * -1 if there was an error
 */
template <typename T>
int ExtractFromNetworkBuffer(T &data, uint8_t *buf)
{
  constexpr bool is_int8 = std::is_same<T, int8_t>::value;
  constexpr bool is_uint8 = std::is_same<T, uint8_t>::value;
  constexpr bool is_int16 = std::is_same<T, int16_t>::value;
  constexpr bool is_uint16 = std::is_same<T, uint16_t>::value;
  constexpr bool is_int32 = std::is_same<T, int32_t>::value;
  constexpr bool is_uint32 = std::is_same<T, uint32_t>::value;
  constexpr bool is_int64 = std::is_same<T, int64_t>::value;
  constexpr bool is_uint64 = std::is_same<T, uint64_t>::value;

  if (is_uint8 || is_int8) {
    data = *(reinterpret_cast<T *>(buf));
  } else if (is_uint16 || is_int16) {
    data = ntohs(*(reinterpret_cast<T *>(buf)));
  } else if (is_uint32 || is_int32) {
    data = ntohl(*(reinterpret_cast<T *>(buf)));
  } else if (is_uint64 || is_int64) {
    data = hostToNetwork(*(reinterpret_cast<T *>(buf)));
  } else {
    return -1;
  }

  return sizeof(T);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif //_MSC_VER

} // namespace joescan

#endif // JOESCAN_TCP_SERIALIZATION_HELPERS_H
