#pragma once
// Bosch BME280 (temperature + humidity + pressure) and BMP280 (temperature +
// pressure), I2C 0x76 or 0x77. One driver, the chip ID decides which it is.
// Forced mode, 1x oversampling: one measurement per trigger, then it sleeps
// (no self-heating). Compensation = Bosch datasheet floating-point formulas.
#include "SensorCore.h"

class BMx280 : public I2cSensor {
 public:
  using I2cSensor::I2cSensor;
  SensorKind kind() const override { return isBme_ ? SK_BME280 : SK_BMP280; }

  bool detect() override {
    uint8_t id;
    if (!i2c::readRegs(addr_, 0xD0, &id, 1)) return false;
    if (id == 0x60) { isBme_ = true;  return true; }
    if (id >= 0x56 && id <= 0x58) { isBme_ = false; return true; }
    return false;
  }

  bool begin() override {
    i2c::writeReg(addr_, 0xE0, 0xB6);  // soft reset
    delay(5);
    for (uint8_t i = 0; i < 20; i++) {  // wait for the NVM copy
      uint8_t st;
      if (i2c::readRegs(addr_, 0xF3, &st, 1) && !(st & 0x01)) break;
      delay(2);
    }
    uint8_t c[26];
    if (!i2c::readRegs(addr_, 0x88, c, 26)) return false;
    T1 = u16(c, 0); T2 = s16(c, 2); T3 = s16(c, 4);
    P1 = u16(c, 6); P2 = s16(c, 8); P3 = s16(c, 10); P4 = s16(c, 12); P5 = s16(c, 14);
    P6 = s16(c, 16); P7 = s16(c, 18); P8 = s16(c, 20); P9 = s16(c, 22);
    if (isBme_) {
      H1 = c[25];
      uint8_t h[7];
      if (!i2c::readRegs(addr_, 0xE1, h, 7)) return false;
      H2 = s16(h, 0);
      H3 = h[2];
      H4 = (int16_t)(((int8_t)h[3]) * 16 | (h[4] & 0x0F));
      H5 = (int16_t)(((int8_t)h[5]) * 16 | (h[4] >> 4));
      H6 = (int8_t)h[6];
      i2c::writeReg(addr_, 0xF2, 0x01);  // humidity x1 (must precede 0xF4)
    }
    return i2c::writeReg(addr_, 0xF5, 0x00) && T1 != 0;  // no filter, calibration sane
  }

  // temperature x1, pressure x1, forced mode -> ~8 ms
  bool trigger() override { return i2c::writeReg(addr_, 0xF4, 0x25); }

  bool fetch(Reading& r) override {
    uint8_t d[8];
    if (!i2c::readRegs(addr_, 0xF7, d, isBme_ ? 8 : 6)) return false;
    const int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    const int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    if (adcT == 0x80000) { r.diag = 0; return false; }  // measurement skipped

    double v1 = (adcT / 16384.0 - T1 / 1024.0) * T2;
    double v2 = (adcT / 131072.0 - T1 / 8192.0);
    v2 = v2 * v2 * T3;
    const double tFine = v1 + v2;
    r.t = (float)(tFine / 5120.0);

    if (adcP != 0x80000) {
      v1 = tFine / 2.0 - 64000.0;
      v2 = v1 * v1 * P6 / 32768.0;
      v2 = v2 + v1 * P5 * 2.0;
      v2 = v2 / 4.0 + P4 * 65536.0;
      v1 = (P3 * v1 * v1 / 524288.0 + P2 * v1) / 524288.0;
      v1 = (1.0 + v1 / 32768.0) * P1;
      if (v1 != 0.0) {
        double p = 1048576.0 - adcP;
        p = (p - v2 / 4096.0) * 6250.0 / v1;
        v1 = P9 * p * p / 2147483648.0;
        v2 = p * P8 / 32768.0;
        p = p + (v1 + v2 + P7) / 16.0;
        r.p = (float)(p / 100.0);  // Pa -> hPa
      }
    }

    if (isBme_) {
      const int32_t adcH = ((int32_t)d[6] << 8) | d[7];
      if (adcH != 0x8000) {
        double h = tFine - 76800.0;
        h = (adcH - (H4 * 64.0 + H5 / 16384.0 * h)) *
            (H2 / 65536.0 * (1.0 + H6 / 67108864.0 * h * (1.0 + H3 / 67108864.0 * h)));
        h = h * (1.0 - H1 * h / 524288.0);
        r.h = (float)constrain(h, 0.0, 100.0);
      }
    }
    return true;
  }

 private:
  static uint16_t u16(const uint8_t* b, uint8_t i) { return (uint16_t)(b[i] | b[i + 1] << 8); }
  static int16_t  s16(const uint8_t* b, uint8_t i) { return (int16_t)u16(b, i); }

  bool     isBme_ = false;
  uint16_t T1 = 0, P1 = 0;
  int16_t  T2 = 0, T3 = 0, P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
  uint8_t  H1 = 0, H3 = 0;
  int16_t  H2 = 0, H4 = 0, H5 = 0;
  int8_t   H6 = 0;
};
