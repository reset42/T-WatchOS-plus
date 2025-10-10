#pragma once
#include <Arduino.h>
#include <vector>
#include "os/system_config.hpp"
#include "drivers/pmu_axp2101.hpp"

class ApiBus;
class DisplayService;

class PowerService {
public:
  enum class Mode : uint8_t { Ready=0, Standby=1, LightSleep=2 };
  static const char* modeName(Mode m);

  enum class Profile : uint8_t { Balanced=0, Performance=1, Endurance=2 };
  static const char* profileName(Profile p);

  // Nutzeraktivitäten
  enum class Activity : uint8_t { Touch=0, Button=1, Radio=2 };

  struct WakePolicy {
    enum class ButtonShort : uint8_t { None=0, ToggleReadyStandby=1 };
    ButtonShort button_short = ButtonShort::ToggleReadyStandby;
    bool touch  = true;
    bool motion = false;
    bool radio_event = true;
  };

  struct QuietPolicy {
    bool     enable = false;
    uint16_t start_min = 23*60;
    uint16_t end_min   = 7*60;
    bool     screen_on_on_event = false;
    bool     haptics            = false;
    uint8_t  bl_cap_pct         = 60; // 10..100 (als Multiplikator)
  };

  struct RadioPolicy {
    enum class Mode3 : uint8_t { Off=0, On=1, Auto=2 };
    enum class LoRaRx : uint8_t { Off=0, Periodic=1, Always=2 };
    Mode3   ble  = Mode3::Auto;
    Mode3   wifi = Mode3::Off;
    LoRaRx  lora = LoRaRx::Periodic;
    uint16_t lora_period_s = 60;
  };

  enum class LeaseType : uint8_t { KEEP_AWAKE=0, BL_PULSE=1, LORA_RX=2 };

  PowerService();

  void begin(const SystemConfig& cfg, PMU_AXP2101* pmu);
  void attachApi(ApiBus& api);
  void attachDisplay(DisplayService& dsp);

  void loop();

  // Modi
  void requestMode(Mode m);
  void setMode(Mode m);
  Mode mode() const { return _mode; }

  // Profile
  void applyProfile(Profile p);
  Profile getProfile() const { return _profile; }

  // Timeouts/Brightness
  void setTimeouts(uint16_t ready_s, uint16_t standby_to_ls_s);
  uint16_t getReadyTimeoutS() const { return _ready_s; }
  uint16_t getStandbyToLSTimeoutS() const { return _standby_to_ls_s; }

  void setReadyBrightness(uint8_t duty);
  uint8_t getReadyBrightnessDuty() const { return _ready_bl_duty; }
  uint8_t getBacklightDutyNow() const;

  // Leases
  uint16_t addLease(LeaseType t, uint32_t ttl_ms);
  void dropLease(uint16_t id);

  // Policies
  WakePolicy  getWakePolicy() const;
  void        setWakePolicy(const WakePolicy& w);

  QuietPolicy getQuiet() const;
  void        setQuiet(const QuietPolicy& q);
  uint8_t     getQuietCapPct() const { return _quiet_cap_pct; }
  void        setQuietCapPct(uint8_t pct);

  RadioPolicy getRadioPolicy() const;
  void        setRadioPolicy(const RadioPolicy& r);

  // Sonstiges
  void setAvoidLightSleepWhenUSB(bool en) { _avoid_ls_usb = en; }
  void userActivity(Activity a);

  void onPmuEvent(const PMU_AXP2101::Event& e);

  // Zeit für Quiet-Fenster (Stub/ohne RTC)
  uint16_t getNowMin() const { return _now_min; }
  void setNowMin(uint16_t mm) { _now_min = (uint16_t)constrain((int)mm, 0, 1439); }

private:
  void apply_rail_for_mode_(Mode m);
  void evt_mode_(const char* m);
  void recompute_now_min_();

  bool     isQuietNow_() const;
  uint8_t  applyQuietCap_(uint8_t duty) const;

  struct LeaseEntry {
    uint16_t id;
    LeaseType type;
    uint32_t expire_ms;
  };

  SystemConfig    _cfg{};
  PMU_AXP2101*    _pmu{nullptr};
  ApiBus*         _api{nullptr};
  DisplayService* _dsp{nullptr};

  Mode            _mode{Mode::Standby};
  Profile         _profile{Profile::Balanced};

  uint16_t  _ready_s{20};
  uint16_t  _standby_to_ls_s{45};
  uint8_t   _ready_bl_duty{160};

  uint8_t   _quiet_cap_pct{60};
  uint16_t  _now_min{0};

  uint16_t  _mv_aldo2{3300};
  bool      _avoid_ls_usb{true};

  std::vector<LeaseEntry> _leases{};
  uint16_t _next_lease_id{1};

  uint32_t _last_activity_ms{0};
};
