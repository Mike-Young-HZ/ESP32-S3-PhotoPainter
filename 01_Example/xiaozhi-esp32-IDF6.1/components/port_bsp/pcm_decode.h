#pragma once

#include <cstddef>
#include <cstdint>
#include <zlib.h>

// Four-byte little-endian decoded length followed by a zlib stream.
inline uint32_t PcmDecodedSize(const uint8_t* data, size_t size) {
    if (data == nullptr || size <= 4) return 0;
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
           (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

inline bool DecodePcm(const uint8_t* data, size_t size, uint8_t* output, size_t capacity) {
    uint32_t expected = PcmDecodedSize(data, size);
    if (expected == 0 || output == nullptr || expected > capacity) return false;
    uLongf actual = expected;
    return uncompress(output, &actual, data + 4, size - 4) == Z_OK && actual == expected;
}
