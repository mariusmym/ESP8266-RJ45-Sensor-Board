#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SensorCore — shared pieces of the sensor driver collection
//
//  Every I2C driver derives from I2cSensor and implements four calls:
//    detect()  : is THIS chip type answering at this address? (checks an ID or
//                a CRC-protected reply, so it never claims someone else's chip)
//    begin()   : one-time setup after detect (calibration data, reset, ...)
//    trigger() : start a measurement, returns immediately
//    fetch()   : read the result (called ~2 s after trigger, so never waits)
//
//  To add a new sensor type: add a SensorKind + a KINDS[] row below, write a
//  driver class like SHT3x.h, and add it to identifyI2c() in Sensors.h.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>

enum SensorKind : uint8_t {
  SK_NONE = 0,
  SK_DS18B20,
  SK_SHT3X,
  SK_SHT4X,
  SK_AHT2X,
  SK_BME280,
  SK_BMP280,
  SK_MAX6675,
  SK_COUNT
};

struct KindInfo {
  const char* code;   // JSON "kind"
  const char* model;  // shown on the web page
  const char* name;   // default OLED name (keep it short, max ~7 chars)
  bool  hum;          // has relative humidity
  bool  pres;         // has barometric pressure
  float tMin, tMax;   // measuring range, also the allowed alarm range
};

static const KindInfo KINDS[SK_COUNT] = {
  { "none",    "-",                 "-",       false, false,   0.0f,    0.0f },
  { "ds",      "DS18B20",           "DS",      false, false, -55.0f,  125.0f },
  { "sht3x",   "SHT30 / SHT31 / SHT35", "SHT3x", true,  false, -40.0f,  125.0f },
  { "sht4x",   "SHT40 / SHT41 / SHT45", "SHT4x", true,  false, -40.0f,  125.0f },
  { "aht2x",   "AHT20 / AHT21",     "AHT2x",   true,  false, -40.0f,   85.0f },
  { "bme280",  "BME280",            "BME280",  true,  true,  -40.0f,   85.0f },
  { "bmp280",  "BMP280",            "BMP280",  false, true,  -40.0f,   85.0f },
  { "max6675", "MAX6675 thermocouple", "TC",   false, false,   0.0f, 1024.0f },
};

inline const KindInfo& kindInfo(uint8_t k) { return KINDS[k < SK_COUNT ? k : (uint8_t)SK_NONE]; }

struct Reading {
  float t = NAN;     // °C
  float h = NAN;     // %RH
  float p = NAN;     // hPa
  float diag = NAN;  // raw value kept for diagnostics when the read is bad
};

// ── I2C helpers ──────────────────────────────────────────────────────────────
// Sensors on RJ45 cables get a slow, forgiving clock. The OLED driver (U8g2)
// sets its own faster clock at the start of every frame, so both coexist.
#ifndef I2C_SENSOR_HZ
#define I2C_SENSOR_HZ 50000
#endif

namespace i2c {

inline void slow() { Wire.setClock(I2C_SENSOR_HZ); }

inline bool ping(uint8_t addr) {
  slow();
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

inline bool write(uint8_t addr, const uint8_t* d, uint8_t n) {
  slow();
  Wire.beginTransmission(addr);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}

inline bool cmd8(uint8_t addr, uint8_t c) { return write(addr, &c, 1); }

inline bool cmd16(uint8_t addr, uint16_t c) {
  const uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c };
  return write(addr, b, 2);
}

inline bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  const uint8_t b[2] = { reg, v };
  return write(addr, b, 2);
}

inline bool read(uint8_t addr, uint8_t* d, uint8_t n) {
  slow();
  if (Wire.requestFrom(addr, n) != n) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

inline bool readRegs(uint8_t addr, uint8_t reg, uint8_t* d, uint8_t n) {
  slow();
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  return read(addr, d, n);
}

// Sensirion / Aosong CRC-8: polynomial 0x31, init 0xFF
inline uint8_t crc8(const uint8_t* d, uint8_t n) {
  uint8_t c = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    c ^= d[i];
    for (uint8_t b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

// two data bytes followed by their CRC (Sensirion word format)
inline bool wordOk(const uint8_t* d) { return crc8(d, 2) == d[2]; }

}  // namespace i2c

// ── Base class for I2C sensors ───────────────────────────────────────────────
class I2cSensor {
 public:
  explicit I2cSensor(uint8_t addr) : addr_(addr) {}
  virtual ~I2cSensor() {}
  virtual bool detect() = 0;
  virtual bool begin() { return true; }
  virtual bool trigger() = 0;
  virtual bool fetch(Reading& r) = 0;
  virtual SensorKind kind() const = 0;
  uint8_t addr() const { return addr_; }

 protected:
  uint8_t addr_;
};
