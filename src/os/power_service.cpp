#include "os/power_service.hpp"
#include "os/api_bus.hpp"
#include "os/display_service.hpp"
#include "drivers/board_pins.hpp"
#include "drivers/pmu_axp2101.hpp"
#include <esp_sleep.h>
#include <driver/gpio.h>

extern DevConfig g_dev;

// --- File-scope Helfer/State (konservativ, eine Instanz im System) ----------
static uint32_t s_last_ready_enter_ms = 0;
static bool     s_pending_enter_ls    = false;

static const char* _mode_name(PowerService::Mode m) {
  switch (m) {
    case PowerService::Mode::Ready:      return "ready";
    case PowerService::Mode::Standby:    return "standby";
    case PowerService::Mode::LightSleep: return "lightsleep";
  }
  return "standby";
}

const char* PowerService::modeName(PowerService::Mode m) { return _mode_name(m); }

const char* PowerService::profileName(PowerService::Profile p) {
  switch (p) {
    case Profile::Performance: return "performance";
    case Profile::Endurance:   return "endurance";
    case Profile::Balanced:
    default:                   return "balanced";
  }
}

PowerService::PowerService() {}

void PowerService::begin(const SystemConfig& cfg, PMU_AXP2101* pmu) {
  _cfg = cfg;
  _pmu = pmu;

  _ready_s           = cfg.display.timeout_ready_s;
  _standby_to_ls_s   = cfg.display.timeout_standby_to_lightsleep_s;
  _quiet_cap_pct     = cfg.quiet_bl_cap_pct;

  _mv_aldo2 = g_dev.rails.backlight_mV ? g_dev.rails.backlight_mV : 3300;

  apply_rail_for_mode_(Mode::Standby);
  _mode = Mode::Standby;

  _last_activity_ms = millis();         // Inaktivitäts-Basis setzen
  recompute_now_min_();
  evt_mode_("standby");
}

void PowerService::attachApi(ApiBus& api) { _api = &api; }
void PowerService::attachDisplay(DisplayService& dsp) { _dsp = &dsp; }

void PowerService::loop() {
  const uint32_t now = millis();
  bool changed = false;

  // 0) Leases: abgelaufene entfernen + Flags bilden
  bool has_keep_awake = false;
  bool has_bl_pulse   = false;
  bool has_lora_rx    = false;

  for (size_t i = 0; i < _leases.size();) {
    if ((int32_t)(_leases[i].expire_ms - now) <= 0) {
      _leases.erase(_leases.begin()+i);
      changed = true;
    } else {
      switch (_leases[i].type) {
        case LeaseType::KEEP_AWAKE: has_keep_awake = true; break;
        case LeaseType::BL_PULSE:   has_bl_pulse   = true; break;
        case LeaseType::LORA_RX:    has_lora_rx    = true; break;
      }
      ++i;
    }
  }
  if (changed && _api) _api->publishEvent("power/lease_changed", {});

  // 1) BL_PULSE streckt Timeouts (wie Aktivität)
  if (has_bl_pulse) {
    _last_activity_ms = now;
  }

  // 2) Inaktivitäts-Automatik (respektiert Leases/USB)
  const uint32_t dt_ms = now - _last_activity_ms;

  if (_mode == Mode::Ready) {
    const uint32_t t_ready_ms = (uint32_t)_ready_s * 1000U;
    if (!has_keep_awake && dt_ms >= t_ready_ms) {
      requestMode(Mode::Standby);
      // early return vermeidet, dass wir im gleichen Loop weiterlaufen
      return;
    }
  } else if (_mode == Mode::Standby) {
    const uint32_t t_ls_ms = (uint32_t)_standby_to_ls_s * 1000U;
    const bool usb_blocks  = g_dev.debug_avoid_ls_when_usb;
    if (!has_keep_awake && !usb_blocks && !has_lora_rx && dt_ms >= t_ls_ms) {
      requestMode(Mode::LightSleep);
      return;
    }
  }

  // 3) Non-blocking LightSleep Eintritt, sobald BL wirklich 0 ist
  if (s_pending_enter_ls) {
    if (_dsp && _dsp->getBacklightDuty() == 0) {
      s_pending_enter_ls = false;
      // CPU schlafen legen; Wakequelle(n) wurden in setMode() konfiguriert
      esp_light_sleep_start();
      // Nach IRQ-Wake direkt in READY wechseln
      requestMode(Mode::Ready);
    }
  }
}

// (Achtung: mode() ist im Header inline definiert – keine out-of-line Definition hier!)

void PowerService::requestMode(Mode m) { setMode(m); }

void PowerService::setMode(Mode m) {
  if (_mode == m) return;

  // 1) USB: LS optional vermeiden (nur bei explizitem/verdecktem LS)
  if (m == Mode::LightSleep && g_dev.debug_avoid_ls_when_usb) {
    m = Mode::Standby;
    if (_api) _api->publishEvent("power/info", { {"ls_skipped","usb"} });
  }

  // 2) Rails passend schalten
  apply_rail_for_mode_(m);

  // 3) Display Hooks (führen die Rampen nach DevConfig aus)
  if (_dsp) {
    switch (m) {
      case Mode::Ready:      _dsp->onReady();      break;
      case Mode::Standby:    _dsp->onStandby();    break;
      case Mode::LightSleep: _dsp->onLightSleep(); break;
    }
  }

  // 4) Modus übernehmen
  _mode = m;

  // 5) READY: Aufwachzeit merken (für min_awake_ms)
  if (_mode == Mode::Ready) {
    s_last_ready_enter_ms = millis();
    _last_activity_ms     = s_last_ready_enter_ms;   // frisch wach → Timer zurücksetzen
  }

  // 6) LIGHTSLEEP: Eintritt non-blocking, nur PMU-IRQ als Wakequelle
  if (_mode == Mode::LightSleep) {
    // Wake nur über PMU-Button (AXP INT), Touch NICHT als LS-Wake
    // Konfig: open-drain, active-low → Low-level Wake
    gpio_wakeup_enable((gpio_num_t)TWATCH_S3_PMU_Pins::PMU_IRQ, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    s_pending_enter_ls = true;
  }

  // 7) Event
  evt_mode_( _mode_name(m) );
}

void PowerService::apply_rail_for_mode_(Mode m) {
  if (!_pmu) return;
  switch (m) {
    case Mode::Ready:      _pmu->setBacklightRail(_mv_aldo2, true);  break;
    case Mode::Standby:    _pmu->setBacklightRail(_mv_aldo2, false); break;
    case Mode::LightSleep: _pmu->setBacklightRail(_mv_aldo2, false); break;
  }
}

void PowerService::applyProfile(Profile p) {
  _profile = p;
  switch (p) {
    case Profile::Balanced:
      _ready_bl_duty = 160; // Profil-Default; Display-Hook übernimmt Rampen
      _ready_s = _cfg.display.timeout_ready_s;
      _standby_to_ls_s = _cfg.display.timeout_standby_to_lightsleep_s;
      break;
    case Profile::Performance:
      _ready_bl_duty = 220;
      _ready_s = max<uint16_t>(_cfg.display.timeout_ready_s, 30);
      _standby_to_ls_s = max<uint16_t>(_cfg.display.timeout_standby_to_lightsleep_s, 60);
      break;
    case Profile::Endurance:
      _ready_bl_duty = 120;
      _ready_s = min<uint16_t>(_cfg.display.timeout_ready_s, 15);
      _standby_to_ls_s = min<uint16_t>(_cfg.display.timeout_standby_to_lightsleep_s, 30);
      break;
  }
  if (_mode == Mode::Ready && _dsp) setReadyBrightness(_ready_bl_duty);
}

void PowerService::setTimeouts(uint16_t ready_s, uint16_t standby_to_ls_s) {
  _ready_s = ready_s;
  _standby_to_ls_s = standby_to_ls_s;
}

void PowerService::setReadyBrightness(uint8_t duty) {
  _ready_bl_duty = duty;
  if (_mode == Mode::Ready && _dsp) {
    const uint8_t eff = applyQuietCap_(_ready_bl_duty);
    _dsp->setBacklightDuty(eff);
  }
}

uint8_t PowerService::getBacklightDutyNow() const {
  return _dsp ? _dsp->getBacklightDuty() : _ready_bl_duty;
}

uint16_t PowerService::addLease(LeaseType t, uint32_t ttl_ms) {
  const uint16_t id = _next_lease_id++;
  _leases.push_back(LeaseEntry{ id, t, millis() + ttl_ms });
  if (_api) _api->publishEvent("power/lease_added", { {"id", String(id)} });
  return id;
}

void PowerService::dropLease(uint16_t id) {
  for (size_t i=0;i<_leases.size();++i) {
    if (_leases[i].id == id) {
      _leases.erase(_leases.begin()+i);
      if (_api) _api->publishEvent("power/lease_dropped", { {"id", String(id)} });
      return;
    }
  }
}

PowerService::WakePolicy PowerService::getWakePolicy() const {
  WakePolicy w;
  w.button_short = (_cfg.wake_button_short == "toggle_ready_standby") ? WakePolicy::ButtonShort::ToggleReadyStandby
                                                                      : WakePolicy::ButtonShort::None;
  w.touch       = _cfg.wake_touch;
  w.motion      = _cfg.wake_motion;
  w.radio_event = _cfg.wake_radio_event;
  return w;
}

void PowerService::setWakePolicy(const WakePolicy& w) {
  _cfg.wake_button_short = (w.button_short == WakePolicy::ButtonShort::ToggleReadyStandby) ? "toggle_ready_standby" : "none";
  _cfg.wake_touch        = w.touch;
  _cfg.wake_motion       = w.motion;
  _cfg.wake_radio_event  = w.radio_event;
}

PowerService::QuietPolicy PowerService::getQuiet() const {
  QuietPolicy q;
  q.enable            = _cfg.quiet_enable;
  q.start_min         = _cfg.quiet_start_min;
  q.end_min           = _cfg.quiet_end_min;
  q.screen_on_on_event= _cfg.quiet_screen_on_on_event;
  q.haptics           = _cfg.quiet_haptics;
  q.bl_cap_pct        = _cfg.quiet_bl_cap_pct;
  return q;
}

void PowerService::setQuiet(const QuietPolicy& q) {
  _cfg.quiet_enable            = q.enable;
  _cfg.quiet_start_min         = q.start_min;
  _cfg.quiet_end_min           = q.end_min;
  _cfg.quiet_screen_on_on_event= q.screen_on_on_event;
  _cfg.quiet_haptics           = q.haptics;
  _cfg.quiet_bl_cap_pct        = q.bl_cap_pct;
  _quiet_cap_pct               = q.bl_cap_pct;

  // Wenn wir gerade Ready sind, die ggf. geänderte Cap sofort anwenden
  if (_mode == Mode::Ready && _dsp) {
    _dsp->setBacklightDuty(applyQuietCap_(_ready_bl_duty));
  }
}

void PowerService::setQuietCapPct(uint8_t pct) {
  _quiet_cap_pct = (uint8_t)constrain((int)pct, 10, 100);
  _cfg.quiet_bl_cap_pct = _quiet_cap_pct;
  if (_mode == Mode::Ready && _dsp) {
    _dsp->setBacklightDuty(applyQuietCap_(_ready_bl_duty));
  }
}

PowerService::RadioPolicy PowerService::getRadioPolicy() const {
  RadioPolicy r;
  auto toMode3 = [](const String& s)->RadioPolicy::Mode3 {
    if (s.equalsIgnoreCase("off")) return RadioPolicy::Mode3::Off;
    if (s.equalsIgnoreCase("on"))  return RadioPolicy::Mode3::On;
    return RadioPolicy::Mode3::Auto;
  };
  auto toLoRa = [](const String& s)->RadioPolicy::LoRaRx {
    if (s.equalsIgnoreCase("off"))      return RadioPolicy::LoRaRx::Off;
    if (s.equalsIgnoreCase("always"))   return RadioPolicy::LoRaRx::Always;
    return RadioPolicy::LoRaRx::Periodic;
  };
  r.ble  = toMode3(_cfg.radio_ble);
  r.wifi = toMode3(_cfg.radio_wifi);
  r.lora = toLoRa(_cfg.lora_rx_policy);
  r.lora_period_s = _cfg.lora_period_s;
  return r;
}

void PowerService::setRadioPolicy(const RadioPolicy& r) {
  auto mode3ToStr = [](RadioPolicy::Mode3 m)->String {
    return (m==RadioPolicy::Mode3::Off)?"off":(m==RadioPolicy::Mode3::On)?"on":"auto";
  };
  auto lrxToStr = [](RadioPolicy::LoRaRx m)->String {
    return (m==RadioPolicy::LoRaRx::Off)?"off":(m==RadioPolicy::LoRaRx::Periodic)?"periodic":"always";
  };
  _cfg.radio_ble       = mode3ToStr(r.ble);
  _cfg.radio_wifi      = mode3ToStr(r.wifi);
  _cfg.lora_rx_policy  = lrxToStr(r.lora);
  _cfg.lora_period_s   = r.lora_period_s;
}

void PowerService::userActivity(Activity a) {
  _last_activity_ms = millis();
  // Touch-Policy: Touch weckt nur aus STANDBY (nicht aus LightSleep)
  if (a == Activity::Touch && _mode == Mode::Standby) {
    requestMode(Mode::Ready);
  }
}

void PowerService::onPmuEvent(const PMU_AXP2101::Event& e) {
  using ET = PMU_AXP2101::EventType;
  switch (e.type) {
    case ET::BUTTON_SHORT: {
      // Toggle READY <-> STANDBY, mit Mindest-Aufwachzeit
      if (_mode == Mode::Ready) {
        const uint32_t min_awake = (uint32_t)g_dev.power_dev.min_awake_ms;
        if (millis() - s_last_ready_enter_ms < min_awake) {
          if (_api) _api->publishEvent("power/info", { {"ignore","min_awake_ms"} });
          break;
        }
        requestMode(Mode::Standby);
      } else {
        requestMode(Mode::Ready);
      }
    } break;

    case ET::BUTTON_LONG:
      // vorerst keine Belegung
      break;

    default:
      // andere PMU-Ereignisse derzeit ohne Änderung
      break;
  }
}

void PowerService::evt_mode_(const char* m) {
  if (!_api) return;
  _api->publishEvent("power/mode_changed", { {"mode", String(m)} });
}

void PowerService::recompute_now_min_() {
  _now_min = _now_min % 1440;
}

// ---------- Quiet-Helfer ----------
bool PowerService::isQuietNow_() const {
  if (!_cfg.quiet_enable) return false;
  const uint16_t s = _cfg.quiet_start_min;
  const uint16_t e = _cfg.quiet_end_min;
  const uint16_t n = _now_min % 1440;
  if (s <= e) return (n >= s && n < e);
  // Fenster über Mitternacht
  return (n >= s || n < e);
}

uint8_t PowerService::applyQuietCap_(uint8_t duty) const {
  if (!isQuietNow_()) return duty;
  // Cap als Multiplikator (z.B. 30 -> ~30% des gewünschten Duty)
  uint32_t scaled = (uint32_t)duty * (uint32_t)_quiet_cap_pct / 100U;
  if (scaled > 255U) scaled = 255U;
  return (uint8_t)scaled;
}
