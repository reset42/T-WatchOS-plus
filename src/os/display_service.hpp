#pragma once
#include <Arduino.h>
#include "os/system_config.hpp"
#include "drivers/pmu_axp2101.hpp"

// Forward Decls
class ApiBus;
class DSP_ST7789V;

class DisplayService {
public:
  DisplayService() = default;

  // Initialisiert Backlight-PWM und -Rail (nutzt dev.conf/system.conf)
  void begin(PMU_AXP2101* pmu, DevConfig const& dev, SystemConfig const& sys);

  // Bus-Bindung (registriert "display.*" Kommandos)
  void attachApi(ApiBus& api);

  // Backlight-Duty (0..255). Rail wird bei begin() vorgewärmt.
  void setBacklightDuty(uint8_t duty);

  // Rail steuern (mV wird intern geclamped)
  void setBacklightRail(uint16_t mv, bool enable);

  // Lebenszyklus-Hinweise (Platzhalter für spätere Panel-Steuerung)
  void onReady();
  void onStandby();
  void onLightSleep();

  // Sanfte Rampen werden nicht-blockierend in loop() gefahren
  void loop();

private:
  // --- Rampen-Logik ---
  void startRamp_(bool to_on, uint8_t duty_target, uint32_t fade_ms, uint16_t mv);
  void publishState_(const char* panel); // evt/display/state

private:
  PMU_AXP2101* _pmu{nullptr};
  ApiBus*      _api{nullptr};

  // LEDC-Konfig (aus dev.conf)
  int      _ledc_ch   = 0;
  uint32_t _ledc_hz   = 1000;
  uint8_t  _ledc_bits = 8;

  // Backlight Rail
  uint16_t _bl_mv     = 3300;
  bool     _rail_on   = false;

  // verhindert unnötige ledcWrite-Aufrufe
  uint8_t  _bl_last   = 255;

  // Rampen-State
  bool      _ramping          = false;
  bool      _ramp_power_on    = false;   // Ziel Panel/Rail
  bool      _rail_off_pending = false;   // nach Fade→0 Rail aus
  uint8_t   _ramp_from        = 0;
  uint8_t   _ramp_to          = 0;
  uint32_t  _ramp_start_ms    = 0;
  uint32_t  _ramp_dur_ms      = 0;
};
