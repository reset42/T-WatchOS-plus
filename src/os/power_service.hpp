#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "drivers/pmu_axp2101.hpp"
#include "os/system_config.hpp"

class ApiBus;
class DisplayService;  // <-- neu forward

class PowerService {
public:
  enum class Mode      : uint8_t { Ready = 0, Standby, LightSleep };
  enum class LeaseType : uint8_t { KEEP_AWAKE = 0, BL_PULSE, LORA_RX };
  enum class Profile   : uint8_t { Performance = 0, Balanced, Endurance };
  enum class Activity  : uint8_t { Touch=0, Button, Radio, Motion };

  struct Lease {
    uint16_t   id{0};
    LeaseType  type{LeaseType::KEEP_AWAKE};
    uint32_t   expires_ms{0};
    bool       active{false};
  };

  struct Params {
    uint8_t  bl_step = 8;
    uint16_t bl_step_ms = 15;
    uint32_t min_awake_ms = 3000;
    uint32_t idle_to_standby_ms = 20000;
    uint32_t idle_to_lightsleep_ms = 45000;
    uint8_t  ready_brightness = 180; // 0..255
  };

  struct WakePolicy {
    bool touch = true;
    bool radio_event = true;
    bool motion = false;
    enum class ButtonShort : uint8_t { ToggleReadyStandby = 0, None } button_short = ButtonShort::ToggleReadyStandby;
  };

  struct Quiet {
    bool     enable = false;
    uint16_t start_min = 23*60;
    uint16_t end_min   = 7*60;
    bool     screen_on_on_event = false;
    bool     haptics = false;
  };

  struct RadioPolicy {
    enum class Mode3 : uint8_t { Off=0, On=1, Auto=2 };
    enum class LoRaRx : uint8_t { Off=0, Periodic=1, Always=2 };
    Mode3    ble  = Mode3::Auto;
    Mode3    wifi = Mode3::Off;
    LoRaRx   lora = LoRaRx::Periodic;
    uint16_t lora_period_s = 60;
  };

  enum class ChargerMode : uint8_t { Auto=0, Never=1 };

  PowerService() = default;
  void begin(SystemConfig const& cfg, PMU_AXP2101* pmu);
  void loop();

  // Injection
  void attachApi(ApiBus& api);
  void attachDisplay(DisplayService* ds) { _display = ds; }  // <-- neu

  // Mode API
  void requestMode(Mode m);
  Mode mode() const { return _mode; }

  void userActivity(Activity src);
  void onPmuEvent(const PMU_AXP2101::Event& e);

  // Lease
  uint16_t addLease(LeaseType type, uint32_t ttl_ms);
  void     dropLease(uint16_t id);

  // Backlight runtime
  void    setReadyBrightness(uint8_t duty);   // 0..255
  uint8_t getReadyBrightnessDuty() const { return _p.ready_brightness; }
  uint8_t getBacklightDutyNow() const     { return _bl_now; }

  // Policies
  void applyProfile(Profile p);
  void setTimeouts(uint16_t ready_s, uint16_t standby_to_ls_s);
  void setWakePolicy(WakePolicy const& w);
  void setQuiet(Quiet const& q);
  void setRadioPolicy(RadioPolicy const& r);
  void setChargerMode(ChargerMode m);

  Profile           getProfile() const           { return _profile; }
  WakePolicy const& getWakePolicy() const        { return _wake; }
  Quiet     const&  getQuiet() const             { return _quiet; }
  RadioPolicy const&getRadioPolicy() const       { return _radio; }
  ChargerMode       getChargerMode() const       { return _charger; }
  uint16_t          getReadyTimeoutS() const     { return (uint16_t)(_p.idle_to_standby_ms/1000UL); }
  uint16_t          getStandbyToLSTimeoutS() const { return (uint16_t)(_p.idle_to_lightsleep_ms/1000UL); }

  // Quiet/Clock
  void     setNowMin(uint16_t minutes_0_1439);
  uint16_t getNowMin() const { return _now_min; }
  bool     isQuietNow() const { return _isQuietNow_(); }
  void     setQuietCapPct(uint8_t pct) { _quiet_bl_cap_pct = constrain(pct, (uint8_t)10, (uint8_t)100); }
  uint8_t  getQuietCapPct() const      { return _quiet_bl_cap_pct; }

  // Debug
  void setAvoidLightSleepWhenUSB(bool en) { _avoid_sleep_when_usb = en; }

  static const char* modeName(Mode m);
  static const char* profileName(Profile p);

private:
  void _applyModeIfNeeded_();
  void _enterReady_();
  void _enterStandby_();
  void _enterLightSleep_();

  void _scheduleIdlePolicy_();
  void _updateBacklight_(uint32_t now);
  void _applyRailsForLeases_();

  // Radio hooks
  void _radioOnReady_();
  void _radioOnStandby_();
  void _radioOnLightSleep_();

  void    _blTarget(uint8_t duty);
  void    _blSetNow_(uint8_t duty);
  uint8_t _pctToDuty_(uint8_t pct) const;

  bool    _isQuietNow_() const;
  uint8_t _capDutyForQuiet_(uint8_t duty) const;

private:
  PMU_AXP2101* _pmu{nullptr};
  DisplayService* _display{nullptr};  // <-- neu
  ApiBus* _api{nullptr};

  SystemConfig _cfg{};
  Params       _p{};

  Mode     _mode{Mode::Ready};
  bool     _mode_dirty{true};

  uint8_t  _bl_now{0};
  uint8_t  _bl_target{0};
  uint32_t _bl_next_step_ms{0};

  uint32_t _t_last_user_ms{0};
  uint32_t _t_enter_ready_ms{0};
  uint32_t _t_last_button_short_ms{0};

  static constexpr uint8_t MAX_LEASES = 8;
  Lease     _leases[MAX_LEASES]{};
  uint16_t  _lease_seq{1};

  WakePolicy   _wake{};
  Quiet        _quiet{};
  RadioPolicy  _radio{};
  ChargerMode  _charger{ChargerMode::Auto};
  Profile      _profile{Profile::Balanced};

  uint16_t _now_min{12*60};
  bool     _lora_wanted{false};
  uint8_t  _quiet_bl_cap_pct{60};
  bool     _avoid_sleep_when_usb{true};
};
