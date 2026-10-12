#pragma once
#include <cstdint>
// crc: Previous CRC32 result, supporting the SDK's incremental calls.
// data: Bytes passed by the real wire-format serializer.
// length: Bounded byte count; this fake replaces only the ROM primitive.
inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *data, uint32_t length)
{
    crc = ~crc;
    for (uint32_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}
