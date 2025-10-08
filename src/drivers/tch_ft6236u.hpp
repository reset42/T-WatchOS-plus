#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

class TCH_FT6236U {
public:
  struct Point {
    uint16_t x;
    uint16_t y;
    uint8_t  id;    // 0..1
    uint8_t  event; // 0=down, 1=up, 2=contact(move)
  };
  struct Report {
    uint8_t count;  // 0..2
    Point   pts[2];
  };

  TCH_FT6236U() : _bus(nullptr), _addr(0x38), _irq(-1), _pending(false), _ok(false) {}

  // Nur Deklarationen im Header!
  bool begin(TwoWire& bus, uint8_t addr7bit, int irq_gpio);
  void end();

  bool isReady()   const { return _ok; }
  bool hasPending() const { return _pending; }
  void clearPending()     { _pending = false; }

  bool readReport(Report& out);

private:
  static void IRAM_ATTR _isrThunk(void* arg);
  bool _readBytes(uint8_t reg, uint8_t* buf, size_t len);

  TwoWire* _bus;
  uint8_t  _addr;
  int      _irq;
  volatile bool _pending;
  bool     _ok;
};
