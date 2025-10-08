#include "os/power_service.hpp"
#include "drivers/board_pins.hpp"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "os/api_bus.hpp"
#include "os/display_service.hpp"

void PowerService::begin(SystemConfig const& cfg, PMU_AXP2101* pmu) {
  _cfg = cfg;
  _pmu = pmu;

  // Helligkeit aus User-Config
  _bl_now = 0;
  _bl_target = constrain((int)_cfg.display.brightness_max, 0, 255);
  _p.ready_brightness = _bl_target;

  // Standard-Profile anwenden
  _avoid_sleep_when_usb = true;
  _quiet_bl_cap_pct = _cfg.quiet_bl_cap_pct;

  // Wake-Policy
  _wake.touch         = _cfg.wake_touch;
  _wake.motion        = _cfg.wake_motion;
  _wake.radio_event   = _cfg.wake_radio_event;
  _wake.button_short  = _cfg.wake_button_short.equalsIgnoreCase("none")
                        ? WakePolicy::ButtonShort::None
                        : WakePolicy::ButtonShort::ToggleReadyStandby;

  _quiet.enable              = _cfg.quiet_enable;
  _quiet.start_min           = _cfg.quiet_start_min;
  _quiet.end_min             = _cfg.quiet_end_min;
  _quiet.screen_on_on_event  = _cfg.quiet_screen_on_on_event;
  _quiet.haptics             = _cfg.quiet_haptics;

  _radio.ble   = _cfg.radio_ble.equalsIgnoreCase("on")  ? RadioPolicy::Mode3::On
               : _cfg.radio_ble.equalsIgnoreCase("off") ? RadioPolicy::Mode3::Off
                                                        : RadioPolicy::Mode3::Auto;
  _radio.wifi  = _cfg.radio_wifi.equalsIgnoreCase("on")  ? RadioPolicy::Mode3::On
               : _cfg.radio_wifi.equalsIgnoreCase("off") ? RadioPolicy::Mode3::Off
                                                        : RadioPolicy::Mode3::Auto;
  if      (_cfg.lora_rx_policy.equalsIgnoreCase("off"))      _radio.lora = RadioPolicy::LoRaRx::Off;
  else if (_cfg.lora_rx_policy.equalsIgnoreCase("always"))   _radio.lora = RadioPolicy::LoRaRx::Always;
  else                                                       _radio.lora = RadioPolicy::LoRaRx::Periodic;
  _radio.lora_period_s = _cfg.lora_period_s;

  _charger = _cfg.charger_mode.equalsIgnoreCase("never") ? ChargerMode::Never : ChargerMode::Auto;

  if      (_cfg.power_profile.equalsIgnoreCase("performance")) _profile = Profile::Performance;
  else if (_cfg.power_profile.equalsIgnoreCase("endurance"))   _profile = Profile::Endurance;
  else                                                         _profile = Profile::Balanced;
  applyProfile(_profile);

  _t_last_user_ms         = millis();
  _t_enter_ready_ms       = _t_last_user_ms;
  _t_last_button_short_ms = 0;

  _mode = Mode::Ready;
  _mode_dirty = true;
}

void PowerService::loop() {
  const uint32_t now = millis();
  _scheduleIdlePolicy_();
  _applyModeIfNeeded_();
  _updateBacklight_(now);
  _applyRailsForLeases_();
}

void PowerService::requestMode(Mode m) { if (m != _mode) { _mode = m; _mode_dirty = true; } }

void PowerService::userActivity(Activity src) {
  _t_last_user_ms = millis();
  if ((src == Activity::Touch || src == Activity::Motion) && _mode != Mode::Ready) {
    requestMode(Mode::Ready);
  }
}

void PowerService::onPmuEvent(const PMU_AXP2101::Event& e) {
  switch (e.type) {
    case PMU_AXP2101::EventType::BUTTON_PRESS: userActivity(Activity::Button); break;
    case PMU_AXP2101::EventType::BUTTON_SHORT: {
      uint32_t now = millis();
      if ((now - _t_last_button_short_ms) < 150) return;
      _t_last_button_short_ms = now;
      if (_wake.button_short == WakePolicy::ButtonShort::ToggleReadyStandby) {
        if (_mode == Mode::Ready) requestMode(Mode::Standby);
        else requestMode(Mode::Ready);
      }
      userActivity(Activity::Button);
      break;
    }
    case PMU_AXP2101::EventType::BUTTON_LONG:  break;
    case PMU_AXP2101::EventType::VBUS_IN:
      if (_charger == ChargerMode::Auto && _pmu) {
        _pmu->setVbusLimitMilliamp(_cfg.pmu.vbus_limit_mA);
        _pmu->setChargeTargetMillivolts(_cfg.pmu.charge_target_mV);
      }
      if (_api) _api->publishEvent("charger", {{"state","start"},{"vbus","in"}}, nullptr);
      break;
    case PMU_AXP2101::EventType::VBUS_OUT:
      if (_api) _api->publishEvent("charger", {{"state","done"}}, nullptr);
      break;
    default: break;
  }
}

uint16_t PowerService::addLease(LeaseType type, uint32_t ttl_ms) {
  const uint32_t now = millis();
  for (uint8_t i=0;i<MAX_LEASES;i++) if (!_leases[i].active) {
    _leases[i].active = true;
    _leases[i].type = type;
    _leases[i].expires_ms = now + ttl_ms;
    _leases[i].id = (_lease_seq ? _lease_seq++ : ++_lease_seq);
    if (type == LeaseType::KEEP_AWAKE) requestMode(Mode::Ready);
    if (type == LeaseType::BL_PULSE)   _blTarget(_p.ready_brightness);
    if (type == LeaseType::LORA_RX)    _lora_wanted = true;
    return _leases[i].id;
  }
  return 0;
}

void PowerService::dropLease(uint16_t id) {
  if (!id) return;
  for (auto &L : _leases) if (L.active && L.id == id) { L.active = false; break; }
}

void PowerService::_applyRailsForLeases_() {
  const uint32_t now = millis();
  bool keep_awake_wanted = false;
  bool lora_wanted = false;

  for (auto &L : _leases) {
    if (!L.active) continue;
    if ((int32_t)(L.expires_ms - now) <= 0) { L.active = false; continue; }
    if (L.type == LeaseType::KEEP_AWAKE) keep_awake_wanted = true;
    if (L.type == LeaseType::LORA_RX)    lora_wanted = true;
  }

  if (keep_awake_wanted && _mode != Mode::Ready) requestMode(Mode::Ready);

  if (lora_wanted != _lora_wanted) {
    _lora_wanted = lora_wanted;
    if (_pmu) _pmu->setLoRaRails(_lora_wanted, 3300, 3300);
  }
}

void PowerService::_applyModeIfNeeded_() {
  if (!_mode_dirty) return;
  Mode newMode = _mode;
  _mode_dirty = false;

  switch (newMode) {
    case Mode::Ready:       _enterReady_(); break;
    case Mode::Standby:     _enterStandby_(); break;
    case Mode::LightSleep:  _enterLightSleep_(); break;
  }

  if (_api) _api->publishEvent("power/mode_changed", {{"mode", modeName(newMode)}}, nullptr);
}

void PowerService::_enterReady_() {
  uint8_t duty = _capDutyForQuiet_(_p.ready_brightness);
  _blTarget(duty);
  _t_enter_ready_ms = millis();
  _t_last_user_ms   = _t_enter_ready_ms;
  _radioOnReady_();
  if (_display) _display->onReady();
}

void PowerService::_enterStandby_() {
  _blTarget(0);
  _radioOnStandby_();
  if (_display) _display->onStandby();
  // Standby zählt als Aktivität → verhindert sofortigen Übergang in LightSleep
  _t_last_user_ms = millis();
}

void PowerService::_enterLightSleep_() {
  _blTarget(0);
  _radioOnLightSleep_();
  if (_display) _display->onLightSleep();

  // Wake per PMU-IRQ (LOW-Level)
  pinMode(TWATCH_S3_PMU_Pins::PMU_IRQ, INPUT_PULLUP);
  gpio_wakeup_disable((gpio_num_t)TWATCH_S3_PMU_Pins::PMU_IRQ);
  gpio_wakeup_enable((gpio_num_t)TWATCH_S3_PMU_Pins::PMU_IRQ, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();

  gpio_wakeup_disable((gpio_num_t)TWATCH_S3_PMU_Pins::PMU_IRQ);

  if (_mode == Mode::LightSleep) requestMode(Mode::Standby);
}

void PowerService::_blTarget(uint8_t duty) {
  _bl_target = duty;
  _bl_next_step_ms = 0;
}

void PowerService::setReadyBrightness(uint8_t duty) {
  _p.ready_brightness = duty;
  if (_mode == Mode::Ready) _blTarget(_capDutyForQuiet_(duty));
}

void PowerService::_updateBacklight_(uint32_t now) {
  if (_bl_now == _bl_target) return;
  if (_bl_next_step_ms && (int32_t)(now - _bl_next_step_ms) < 0) return;

  int diff = (int)_bl_target - (int)_bl_now;
  int step = (diff > 0) ? _p.bl_step : -((int)_p.bl_step);
  int next = (int)_bl_now + step;

  if ((step > 0 && next > _bl_target) || (step < 0 && next < _bl_target)) next = _bl_target;

  _blSetNow_((uint8_t)constrain(next, 0, 255));
  _bl_next_step_ms = now + _p.bl_step_ms;
}

void PowerService::_blSetNow_(uint8_t duty) {
  _bl_now = duty;
  if (_display) _display->setBacklightDuty(duty);
}

uint8_t PowerService::_pctToDuty_(uint8_t pct) const {
  return (uint8_t)map((int)pct, 0, 100, 0, 255);
}

void PowerService::applyProfile(Profile p) {
  _profile = p;
  switch (p) {
    case Profile::Performance:
      _p.ready_brightness = constrain(_cfg.display.brightness_max, 0, 255);
      _p.idle_to_standby_ms = 60000;
      _p.idle_to_lightsleep_ms = 300000;
      _p.bl_step = 12; _p.bl_step_ms = 10;
      break;
    case Profile::Endurance:
      _p.ready_brightness = max<uint8_t>(_cfg.display.brightness_min, 100);
      _p.idle_to_standby_ms = 10000;
      _p.idle_to_lightsleep_ms = 30000;
      _p.bl_step = 6; _p.bl_step_ms = 20;
      break;
    case Profile::Balanced:
    default:
      _p.ready_brightness = constrain(_cfg.display.brightness_max, 0, 255);
      _p.idle_to_standby_ms = (uint32_t)_cfg.display.timeout_ready_s * 1000UL;
      _p.idle_to_lightsleep_ms = (uint32_t)_cfg.display.timeout_standby_to_lightsleep_s * 1000UL;
      _p.bl_step = 8; _p.bl_step_ms = 15;
      break;
  }
  if (_mode == Mode::Ready) _blTarget(_capDutyForQuiet_(_p.ready_brightness));
}

void PowerService::setTimeouts(uint16_t ready_s, uint16_t standby_to_ls_s) {
  _p.idle_to_standby_ms    = (uint32_t)ready_s * 1000UL;
  _p.idle_to_lightsleep_ms = (uint32_t)standby_to_ls_s * 1000UL;
}

void PowerService::setWakePolicy(WakePolicy const& w)      { _wake = w; }
void PowerService::setQuiet(Quiet const& q)                 { _quiet = q; }
void PowerService::setRadioPolicy(RadioPolicy const& r)     { _radio = r; }
void PowerService::setChargerMode(ChargerMode m)            { _charger = m; }

bool PowerService::_isQuietNow_() const {
  if (!_quiet.enable) return false;
  uint16_t s = _quiet.start_min, e = _quiet.end_min, n = _now_min;
  if (s == e) return true;
  if (s < e) return (n >= s && n < e);
  return (n >= s || n < e);
}

uint8_t PowerService::_capDutyForQuiet_(uint8_t duty) const {
  if (!_isQuietNow_()) return duty;
  uint8_t cap = (uint8_t) map((int)_quiet_bl_cap_pct, 0, 100, 0, 255);
  return (uint8_t)min<int>(duty, cap);
}

void PowerService::setNowMin(uint16_t minutes_0_1439) {
  _now_min = (uint16_t) constrain(minutes_0_1439, 0, (uint16_t)(24*60-1));
  if (_mode == Mode::Ready) _blTarget(_capDutyForQuiet_(_p.ready_brightness));
}

void PowerService::_radioOnReady_()      { /* später: BLE normal, WiFi on-demand, LoRa policy */ }
void PowerService::_radioOnStandby_()    { /* später: BLE slow-adv, WiFi off, LoRa periodic   */ }
void PowerService::_radioOnLightSleep_() { /* später: alles off außer Wake-Sources            */ }

void PowerService::attachApi(ApiBus& api) { _api = &api; }

const char* PowerService::modeName(Mode m) {
  switch (m) { case Mode::Ready: return "ready"; case Mode::Standby: return "standby"; case Mode::LightSleep: return "lightsleep"; }
  return "ready";
}
const char* PowerService::profileName(Profile p) {
  switch (p) { case Profile::Performance: return "performance"; case Profile::Endurance: return "endurance"; case Profile::Balanced: return "balanced"; }
  return "balanced";
}

/* -------------------------------------------------------
 * Idle-Policy: Ready -> Standby -> LightSleep
 * Respektiert:
 *  - _p.idle_to_standby_ms
 *  - _p.idle_to_lightsleep_ms
 *  - aktive KEEP_AWAKE-Leases
 *  - _avoid_sleep_when_usb (blockt Standby->LS)
 * -----------------------------------------------------*/
void PowerService::_scheduleIdlePolicy_() {
  const uint32_t now = millis();
  const uint32_t idle_ms = now - _t_last_user_ms;

  // Aktive KEEP_AWAKE-Lease?
  bool keep_awake = false;
  for (auto &L : _leases) {
    if (L.active && L.type == LeaseType::KEEP_AWAKE) { keep_awake = true; break; }
  }
  if (keep_awake) return; // niemals herunterstufen, solange Lease aktiv

  switch (_mode) {
    case Mode::Ready:
      if (_p.idle_to_standby_ms && idle_ms >= _p.idle_to_standby_ms) {
        requestMode(Mode::Standby);
      }
      break;

    case Mode::Standby:
      // Optionaler USB-Schutz: Standby bleibt erhalten, kein LS
      if (_avoid_sleep_when_usb) return;

      if (_p.idle_to_lightsleep_ms && idle_ms >= _p.idle_to_lightsleep_ms) {
        requestMode(Mode::LightSleep);
      }
      break;

    case Mode::LightSleep:
      // Wird an anderer Stelle verlassen (IRQ/Wake)
      break;
  }
}
