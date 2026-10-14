#pragma once
// Maxim MAX6675 K-type thermocouple converter, read with bit-banged SPI
// (any 3 pins; on this board RJ5 / the SPI header: SCK=IO14, SO=IO13, CS=IO15).
//
// Detection: the SO line gets the internal pull-up, so with no chip every bit
// reads 1. A real MAX6675 always sends D15 = 0 and D1 = 0 (device ID).
// D2 = 1 means the thermocouple itself is open (probe missing / broken).
#include "SensorCore.h"

class MAX6675 {
 public:
  enum Status : uint8_t { NO_CHIP, OPEN_TC, OK };

  MAX6675(uint8_t sck, uint8_t so, uint8_t cs) : sck_(sck), so_(so), cs_(cs) {}

  void begin() {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    pinMode(sck_, OUTPUT);
    digitalWrite(sck_, LOW);
    pinMode(so_, INPUT_PULLUP);
  }

  // Reading also restarts the conversion (220 ms), so call it at most ~4x/s.
  Status read(Reading& r) {
    const uint16_t v = raw();
    r.diag = v;
    if ((v & 0x8002) != 0) return NO_CHIP;  // floating line reads 0xFFFF
    if (v & 0x0004) return OPEN_TC;
    r.t = (v >> 3) * 0.25f;
    return OK;
  }

 private:
  uint16_t raw() {
    uint16_t v = 0;
    digitalWrite(cs_, LOW);
    delayMicroseconds(10);
    for (int8_t i = 15; i >= 0; i--) {
      digitalWrite(sck_, LOW);
      delayMicroseconds(10);
      if (digitalRead(so_)) v |= (uint16_t)1 << i;
      digitalWrite(sck_, HIGH);
      delayMicroseconds(10);
    }
    digitalWrite(sck_, LOW);
    digitalWrite(cs_, HIGH);
    return v;
  }

  uint8_t sck_, so_, cs_;
};
