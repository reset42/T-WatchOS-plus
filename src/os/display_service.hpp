#pragma once
#include <Arduino.h>
#include "os/system_config.hpp"
#include "drivers/pmu_axp2101.hpp"

// Forward Treiber (optional, noch nicht benutzt)
class DSP_ST7789V;

class DisplayService {
public:
  DisplayService() = default;

  // Initialisiert Backlight-PWM und -Rail (nutzt dev.conf/system.conf)
  void begin(PMU_AXP2101* pmu, DevConfig const& dev, SystemConfig const& sys);

  // Backlight-Duty (0..255). Rail wird bei begin() vorgewärmt.
  void setBacklightDuty(uint8_t duty);

  // Optional Rails direkt steuern (selten gebraucht; normal nur intern)
  void setBacklightRail(uint16_t mv, bool enable);

  // Lifecycle Hooks (Platzhalter – später Display Sleep/Init)
  void onReady();
  void onStandby();
  void onLightSleep();

private:
  PMU_AXP2101* _pmu{nullptr};

  // LEDC-Konfig (aus dev.conf)
  int      _ledc_ch   = 0;
  uint32_t _ledc_hz   = 1000;
  uint8_t  _ledc_bits = 8;

  // Backlight Rail
  uint16_t _bl_mv     = 3300;

  // verhindert unnötige ledcWrite-Aufrufe
  uint8_t  _bl_last   = 255;
};
