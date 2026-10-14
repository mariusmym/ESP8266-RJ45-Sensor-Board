#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  Sensor driver collection for the ESP8266 RJ45 Sensor Board
//
//    1-Wire : DS18B20 (any number, read through OneWire + DallasTemperature)
//    I2C    : SHT30/31/35  0x44 0x45      SHT40/41/45  0x44 0x45
//             AHT20/21     0x38           BME280/BMP280 0x76 0x77
//    SPI    : MAX6675 thermocouple (RJ5 / SPI header, when the port is in SPI mode)
//
//  The sketch scans every 30 s; whatever answers is identified here and gets a
//  sensor slot automatically. Nothing to configure, nothing to recompile.
// ─────────────────────────────────────────────────────────────────────────────
#include "SensorCore.h"
#include "SHT3x.h"
#include "SHT4x.h"
#include "AHT2x.h"
#include "BMx280.h"
#include "MAX6675.h"

namespace sensors {

// one driver instance per (chip type, address): they keep calibration data
inline SHT3x  sht3x44{ 0x44 }, sht3x45{ 0x45 };
inline SHT4x  sht4x44{ 0x44 }, sht4x45{ 0x45 };
inline AHT2x  aht38{ 0x38 };
inline BMx280 bmx76{ 0x76 }, bmx77{ 0x77 };

// addresses worth probing, in scan order
inline const uint8_t I2C_CANDIDATES[] = { 0x38, 0x44, 0x45, 0x76, 0x77 };

// Which supported chip answers at addr? nullptr = none / unknown.
inline I2cSensor* identifyI2c(uint8_t addr) {
  I2cSensor* order[2] = { nullptr, nullptr };
  switch (addr) {
    case 0x38: order[0] = &aht38; break;
    case 0x44: order[0] = &sht3x44; order[1] = &sht4x44; break;
    case 0x45: order[0] = &sht3x45; order[1] = &sht4x45; break;
    case 0x76: order[0] = &bmx76; break;
    case 0x77: order[0] = &bmx77; break;
    default:   return nullptr;
  }
  for (I2cSensor* d : order) {
    if (d && d->detect() && d->begin()) return d;
  }
  return nullptr;
}

// Friendly name for an address seen on the bus (diagnostics only)
inline const char* i2cGuess(uint8_t a) {
  switch (a) {
    case 0x3C: case 0x3D: return "OLED";
    case 0x38: return "AHT?";
    case 0x40: return "HTU21/Si7021/INA219?";
    case 0x23: case 0x5C: return "BH1750?";
    case 0x44: case 0x45: return "SHT?";
    case 0x76: case 0x77: return "BMx280?";
    default:   return "?";
  }
}

}  // namespace sensors
