#pragma once
// Sensirion SHT40 / SHT41 / SHT45, I2C 0x44 or 0x45 (single-byte commands).
#include "SensorCore.h"

class SHT4x : public I2cSensor {
 public:
  using I2cSensor::I2cSensor;
  SensorKind kind() const override { return SK_SHT4X; }

  // "read serial number": two CRC-protected words, only an SHT4x answers it
  bool detect() override {
    if (!i2c::cmd8(addr_, 0x89)) return false;
    delay(2);
    uint8_t d[6];
    return i2c::read(addr_, d, 6) && i2c::wordOk(d) && i2c::wordOk(d + 3);
  }

  bool begin() override {
    i2c::cmd8(addr_, 0x94);  // soft reset
    delay(2);
    return true;
  }

  bool trigger() override { return i2c::cmd8(addr_, 0xFD); }  // high precision, ~9 ms

  bool fetch(Reading& r) override {
    uint8_t d[6];
    if (!i2c::read(addr_, d, 6)) return false;
    if (!i2c::wordOk(d) || !i2c::wordOk(d + 3)) { r.diag = -1; return false; }
    const uint16_t rt = (uint16_t)(d[0] << 8 | d[1]);
    const uint16_t rh = (uint16_t)(d[3] << 8 | d[4]);
    r.t = -45.0f + 175.0f * rt / 65535.0f;
    r.h = constrain(-6.0f + 125.0f * rh / 65535.0f, 0.0f, 100.0f);
    return true;
  }
};
