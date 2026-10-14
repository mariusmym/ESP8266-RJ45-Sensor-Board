#pragma once
// Sensirion SHT30 / SHT31 / SHT35 (same protocol), I2C 0x44 or 0x45.
// Single-shot, high repeatability, no clock stretching (works on long cables).
#include "SensorCore.h"

class SHT3x : public I2cSensor {
 public:
  using I2cSensor::I2cSensor;
  SensorKind kind() const override { return SK_SHT3X; }

  // "read status register" answers with a CRC-protected word only on an SHT3x
  bool detect() override {
    if (!i2c::cmd16(addr_, 0xF32D)) return false;
    delay(2);
    uint8_t d[3];
    return i2c::read(addr_, d, 3) && i2c::wordOk(d);
  }

  bool begin() override {
    i2c::cmd16(addr_, 0x30A2);  // soft reset
    delay(2);
    return i2c::cmd16(addr_, 0x3066);  // heater off
  }

  bool trigger() override { return i2c::cmd16(addr_, 0x2400); }  // ~15 ms

  bool fetch(Reading& r) override {
    uint8_t d[6];
    if (!i2c::read(addr_, d, 6)) return false;
    if (!i2c::wordOk(d) || !i2c::wordOk(d + 3)) { r.diag = -1; return false; }
    const uint16_t rt = (uint16_t)(d[0] << 8 | d[1]);
    const uint16_t rh = (uint16_t)(d[3] << 8 | d[4]);
    r.t = -45.0f + 175.0f * rt / 65535.0f;
    r.h = constrain(100.0f * rh / 65535.0f, 0.0f, 100.0f);
    return true;
  }
};
