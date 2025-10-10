#pragma once
#include <Arduino.h>
#include "os/system_config.hpp"
#include "drivers/pmu_axp2101.hpp"
#include "drivers/dsp_st7789v.hpp"

class ApiBus;

class DisplayService {
public:
  DisplayService() = default;

  void begin(PMU_AXP2101* pmu, const DevConfig& dev, const SystemConfig& sys);
  void attachApi(ApiBus& api);

  // PowerService-Callbacks
  void onReady();
  void onStandby();
  void onLightSleep();

  void loop();  // ohne Argument (passt zu main.cpp)

  // Backlight (direkt setzen)
  void    setBacklightDuty(uint8_t duty);
  uint8_t getBacklightDuty() const { return _bl_last; }

  // Backlight (sanft fahren)
  void setBacklightDutySmooth(uint8_t target,
                              uint8_t step   /* = default_step */,
                              uint16_t ms    /* = default_ms */);

  // Rail-Info (PowerService schaltet Rail, hier nur Zielspannung merken)
  void    setBacklightRail(uint16_t mv, bool enable);

  // Debug
  void drawGrid() { _dsp.drawTestGrid(); }

private:
  PMU_AXP2101* _pmu{nullptr};
  ApiBus*      _api{nullptr};

  // LEDC/PWM
  int      _ledc_ch   = 0;
  uint32_t _ledc_hz   = 1000;
  uint8_t  _ledc_bits = 8;

  // Ziel-mV für BL-Rail (Schalten macht PowerService)
  uint16_t _bl_mv     = 3300;

  // Duty-Tracking
  uint8_t  _bl_last   = 0;

  // Panel-Init-Flag
  bool     _panel_ready{false};

  // Ramping
  bool     _ramp_active{false};
  uint8_t  _ramp_target{0};
  uint8_t  _ramp_step{6};      // Default-Step (≈ 6/255 pro Schritt)
  uint16_t _ramp_ms{6};        // Default Schrittzeit in ms
  uint32_t _ramp_next_ms{0};

  // Ready-Default (kann später aus DevConfig übernommen werden)
  uint8_t  _ready_duty{160};

  DspST7789V _dsp;
};
