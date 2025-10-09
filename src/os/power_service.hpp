#pragma once
#include <Arduino.h>
#include "os/system_config.hpp"
#include "drivers/pmu_axp2101.hpp"

class ApiBus;
class DisplayService;

class PowerService {
public:
  enum class Mode : uint8_t { Ready=0, Standby=1, LightSleep=2 };

  PowerService();

  void begin(SystemConfig const& cfg, PMU_AXP2101* pmu);
  void attachApi(ApiBus& api);
  void attachDisplay(DisplayService& dsp);          // NEU: optional

  void loop();

  void setMode(Mode m);
  Mode mode() const { return _mode; }

  // PMU IRQ -> Bedienung/Koaleszierung etc.
  void onPmuEvent(const PMU_AXP2101::Event& e);

  // User-Aktivität (beeinflusst Idle-Policy)
  enum class Activity : uint8_t { Touch=0, Button=1, App=2 };
  void userActivity(Activity a);

private:
  // Sequenzen
  void enterReady_();
  void enterStandby_();

  // Rails
  void rail_ALDO3_(bool on, uint16_t mv);  // Peripherie 3V3 (Touch/LCD IO)
  void rail_BL_ALDO2_(bool on, uint16_t mv);

  // BL-Ramping
  void startBlRamp_(uint8_t target, uint16_t step_ms);
  void blRampTick_();

  // Events
  void evt_mode_(const char* m);
  void evt_rails_(const char* what, const char* val);
  void evt_panel_(const char* what);
  void evt_blpwm_(uint8_t duty);

private:
  SystemConfig     _cfg;
  PMU_AXP2101*     _pmu;
  ApiBus*          _api;
  DisplayService*  _dsp;

  Mode             _mode;

  // BL-Ramp
  uint8_t   _bl_target;
  uint8_t   _bl_cur;
  uint16_t  _bl_step_ms;
  uint32_t  _bl_last_ms;

  // Dev-Werte
  uint16_t  _mv_aldo2;   // Ziel für BL-Rail
  uint16_t  _mv_aldo3;   // Ziel für Peripherie 3V3
};
