#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "drivers/tch_ft6236u.hpp"
#include "drivers/board_pins.hpp"
#include "os/system_config.hpp"
#include "os/power_service.hpp"

class ApiBus; // Forward

// IRQ-getriebener Touch-Service ohne Rail-Zugriff.
class TouchService {
public:
  TouchService();

  void begin(SystemConfig const& cfg);
  bool begin(TwoWire& bus, int sda, int scl);

  void attachPower(PowerService& pwr);
  inline void attachPowerService(PowerService* p) { if (p) attachPower(*p); }
  void attachApi(ApiBus& api);

  void enable();
  void disable();
  bool isEnabled() const { return _enabled; }

  void loop();
  void simulateTap(uint16_t x, uint16_t y);

private:
  void _onDown(uint16_t x, uint16_t y, uint8_t id);
  void _onMove(uint16_t x, uint16_t y, uint8_t id);
  void _onUp(uint16_t x, uint16_t y, uint8_t id);
  void _applyMapping(uint16_t& x, uint16_t& y) const;
  void _publishRaw(const char* type, uint16_t x, uint16_t y, uint8_t id);
  void _publishSummary_(); // <<< Neu

  // --- Tap-Filter / De-Dupe ---
  uint16_t _downX, _downY;
  uint32_t _downMs;

  bool     _fingerActive;
  uint32_t _lastEvtMs;
  uint16_t _lastX, _lastY;

  // Pfad-Zusammenfassung (für spätere Gesten)
  uint32_t _pathDist1;      // L1-Approx (|dx|+|dy|)
  uint16_t _startX, _startY;

  uint8_t  _rotation; // 0/90/180/270
  bool     _swapXY;
  bool     _invertX;
  bool     _invertY;

  bool         _enabled;
  TCH_FT6236U  _drv;
  TwoWire*     _bus;
  PowerService* _power;
  ApiBus*       _api;

  // Schwellen (Tap-Lock & De-Dupe)
  static constexpr uint16_t TAP_LOCK_RADIUS_PX = 3;    // keine Move-Events im 3px-Radius
  static constexpr uint16_t TAP_LOCK_TIME_MS   = 120;  // in den ersten 120ms nach Down
  static constexpr uint16_t DEDUPE_RADIUS_PX   = 1;    // identische Moves filtern
  static constexpr uint16_t DEDUPE_TIME_MS     = 30;
};
