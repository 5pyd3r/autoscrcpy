#ifndef BINARY_H
#define BINARY_H

#include <stdint.h>

static inline void
write16be(uint8_t *buf, uint16_t value) {
    buf[0] = value >> 8;
    buf[1] = value;
}

static inline void
write32be(uint8_t *buf, uint32_t value) {
    buf[0] = value >> 24;
    buf[1] = value >> 16;
    buf[2] = value >> 8;
    buf[3] = value;
}

static inline void
write64be(uint8_t *buf, uint64_t value) {
    write32be(buf, value >> 32);
    write32be(&buf[4], (uint32_t) value);
}

static inline uint16_t
read16be(const uint8_t *buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

static inline uint32_t
read32be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static inline uint32_t
read32le(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline void
write32le(uint8_t *buf, uint32_t value) {
    buf[0] = value;
    buf[1] = value >> 8;
    buf[2] = value >> 16;
    buf[3] = value >> 24;
}

static inline uint16_t
float_to_u16fp(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return (uint16_t)(f * 0xFFFF);
}

static inline int16_t
float_to_i16fp(float f) {
    if (f < -1.0f) f = -1.0f;
    if (f > 1.0f) f = 1.0f;
    return (int16_t)(f * 0x7FFF);
}

#endif /* BINARY_H */
