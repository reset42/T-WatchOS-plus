#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "drivers/tch_ft6236u.hpp"
#include "drivers/board_pins.hpp"
#include "os/system_config.hpp"
#include "os/power_service.hpp"

class ApiBus; // Forward

// IRQ-getriebener Touch-Service ohne Rail-Zugriff.
// - Unterstützt alte Call-Sites: begin(TwoWire&, int, int) und attachPowerService(PowerService*)
// - Neue, interne Nutzung: begin(SystemConfig const&) + enable()

class TouchService {
public:
  TouchService();

  // Neu (interne Init aus Config; Bus erst bei enable() geöffnet, wenn gewünscht)
  void begin(SystemConfig const& cfg);

  // Kompatibilität zu bestehender main.cpp:
  // Nutzt übergebenen Bus (ohne erneutes Wire.begin), Pins werden hier nicht erneut initialisiert.
  bool begin(TwoWire& bus, int sda, int scl);

  void attachPower(PowerService& pwr);
  // Legacy-Alias für vorhandene Call-Sites:
  inline void attachPowerService(PowerService* p) { if (p) attachPower(*p); }

  void attachApi(ApiBus& api); // vorbereitet

  void enable();   // Initialisiert (falls nötig) Bus1-Variante
  void disable();
  bool isEnabled() const { return _enabled; }

  void loop();

  // Simulationshilfe (später via ApiBus)
  void simulateTap(uint16_t x, uint16_t y);

private:
  void _onDown(uint16_t x, uint16_t y, uint8_t id);
  void _onMove(uint16_t x, uint16_t y, uint8_t id);
  void _onUp(uint16_t x, uint16_t y, uint8_t id);
  void _applyMapping(uint16_t& x, uint16_t& y) const;

  // Zustand
  bool     _fingerActive;
  uint32_t _lastEvtMs;
  uint16_t _lastX, _lastY;

  // Mapping (Rotation/Swap/Invert – später aus cfg)
  uint8_t  _rotation; // 0/90/180/270
  bool     _swapXY;
  bool     _invertX;
  bool     _invertY;

  // Verknüpfungen
  bool         _enabled;
  TCH_FT6236U  _drv;
  TwoWire*     _bus;      // genutzter Bus (vom Aufrufer oder intern Wire1)
  PowerService* _power;
  ApiBus*       _api;
};
