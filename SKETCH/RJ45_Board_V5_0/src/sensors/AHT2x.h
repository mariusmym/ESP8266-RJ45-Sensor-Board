#pragma once
// Aosong AHT20 / AHT21 temperature + humidity, I2C 0x38.
#include "SensorCore.h"

class AHT2x : public I2cSensor {
 public:
  using I2cSensor::I2cSensor;
  SensorKind kind() const override { return SK_AHT2X; }

  // 0x38 is a busy address, so detection runs a full measurement and only
  // accepts a reply with a valid CRC (AHT20/21 send one, AHT10 doesn't).
  bool detect() override {
    calibrate();
    if (!trigger()) return false;
    delay(85);
    Reading r;
    return fetch(r);
  }

  bool trigger() override {
    const uint8_t c[3] = { 0xAC, 0x33, 0x00 };
    return i2c::write(addr_, c, 3);  // ~80 ms
  }

  bool fetch(Reading& r) override {
    uint8_t d[7];
    if (!i2c::read(addr_, d, 7)) return false;
    if (i2c::crc8(d, 6) != d[6] || (d[0] & 0x80)) { r.diag = d[0]; return false; }  // CRC / still busy
    const uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    const uint32_t rt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    r.h = constrain(rh * 100.0f / 1048576.0f, 0.0f, 100.0f);
    r.t = rt * 200.0f / 1048576.0f - 50.0f;
    return true;
  }

 private:
  void calibrate() {
    uint8_t s;
    if (i2c::read(addr_, &s, 1) && !(s & 0x08)) {  // not calibrated yet
      const uint8_t c[3] = { 0xBE, 0x08, 0x00 };
      i2c::write(addr_, c, 3);
      delay(10);
    }
  }
};
