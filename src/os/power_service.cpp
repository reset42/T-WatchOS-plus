#include "os/power_service.hpp"
#include "os/api_bus.hpp"
#include "os/display_service.hpp"

PowerService::PowerService()
: _pmu(nullptr), _api(nullptr), _dsp(nullptr),
  _mode(Mode::Standby),
  _bl_target(0), _bl_cur(0), _bl_step_ms(20), _bl_last_ms(0),
  _mv_aldo2(3300), _mv_aldo3(3300)
{}

void PowerService::begin(SystemConfig const& cfg, PMU_AXP2101* pmu) {
  _cfg = cfg;
  _pmu = pmu;

  // Zielspannungen aus DevConfig/SystemConfig
  // Annahme: cfg.dev.rails.* existiert (wir bleiben bei den bisherigen Keys)
  _mv_aldo2 = cfg.dev.rails.backlight_mV;
  _mv_aldo3 = 3300; // konservativ 3.3V für Peripherie-Rail

  // Default in Standby starten (BL aus, ALDO3 an für Touch/RTC)
  rail_ALDO3_(true,  _mv_aldo3);
  rail_BL_ALDO2_(false, _mv_aldo2);
  _mode = Mode::Standby;
  evt_mode_("standby");
}

void PowerService::attachApi(ApiBus& api) {
  _api = &api;
  api.registerHandler("power", [this, &api](const ApiRequest& r) {
    const String& act = r.action;
    if (act == "set") {
      const String* pm = ApiBus::findParam(r.params, "mode");
      if (!pm) { api.replyErr(r.origin, "param", "missing mode=ready|standby"); return; }
      if (pm->equalsIgnoreCase("ready"))   setMode(Mode::Ready);
      else if (pm->equalsIgnoreCase("standby")) setMode(Mode::Standby);
      else { api.replyErr(r.origin, "param", "invalid mode"); return; }
      api.replyOk(r.origin, { {"mode", pm->c_str()} });
      return;
    }
    api.replyErr(r.origin, "unknown", "power.set mode=ready|standby");
  });
}

void PowerService::attachDisplay(DisplayService& dsp) {
  _dsp = &dsp;
}

void PowerService::setMode(Mode m) {
  if (_mode == m) return;
  switch (m) {
    case Mode::Ready:   enterReady_();   evt_mode_("ready");   break;
    case Mode::Standby: enterStandby_(); evt_mode_("standby"); break;
    default: break;
  }
  _mode = m;
}

void PowerService::enterReady_() {
  // Reihenfolge:
  // 1) ALDO3 an (Peripherie 3V3)
  rail_ALDO3_(true, _mv_aldo3);

  // 2) Panel aufwecken (kein Rail-Zugriff hier)
  if (_dsp) { _dsp->onReady(); evt_panel_("ready"); }

  // 3) BL-Rail an
  rail_BL_ALDO2_(true, _mv_aldo2);

  // 4) sanfte PWM-Rampe (z. B. auf 60% in 20ms-Schritten)
  startBlRamp_(153 /*~60%*/, 18);
}

void PowerService::enterStandby_() {
  // 1) BL-PWM runter
  startBlRamp_(0, 12);
  // Wir lassen das Ramp im loop() auslaufen, Rails werden danach gesetzt
}

void PowerService::loop() {
  blRampTick_();
}

void PowerService::onPmuEvent(const PMU_AXP2101::Event& e) {
  // Hier könnten Button-Short/Long → Mode-Toggles etc. laufen
  (void)e;
}

void PowerService::userActivity(Activity /*a*/) {
  // Placeholder für Idle-Policy
}

// --- Rails & BL --------------------------------------------------------------

void PowerService::rail_ALDO3_(bool on, uint16_t mv) {
  if (!_pmu) return;
  // ALDO3 über XPowersLib (Peripherie 3V3)
  // XPowersAXP2101: setALDO3Voltage / enableALDO3 / disableALDO3
  _pmu->_pmu.setALDO3Voltage(mv);
  if (on) _pmu->_pmu.enableALDO3();
  else    _pmu->_pmu.disableALDO3();
  evt_rails_("aldo3", on ? "on" : "off");
}

void PowerService::rail_BL_ALDO2_(bool on, uint16_t mv) {
  if (!_pmu) return;
  _pmu->setBacklightRail(mv, on);
  evt_rails_("aldo2_bl", on ? "on" : "off");
}

void PowerService::startBlRamp_(uint8_t target, uint16_t step_ms) {
  _bl_target = target;
  if (!_dsp) return; // ohne DisplayService kein PWM
  _bl_step_ms = step_ms ? step_ms : 16;
  _bl_last_ms = millis(); // nächste Tick-Zeit
}

void PowerService::blRampTick_() {
  if (!_dsp) return;
  if (_bl_cur == _bl_target) {
    // Wenn wir auf 0 zielen und dort sind → jetzt BL-Rail aus und Panel schlafen
    if (_bl_target == 0 && _mode == Mode::Standby) {
      rail_BL_ALDO2_(false, _mv_aldo2);
      if (_dsp) { _dsp->onStandby(); evt_panel_("standby"); }
    }
    return;
  }
  const uint32_t now = millis();
  if (now - _bl_last_ms < _bl_step_ms) return;
  _bl_last_ms = now;

  // Schrittweite abhängig von Entfernung (kleine, aber flüssige Rampe)
  int delta = (int)_bl_target - (int)_bl_cur;
  int step  = (delta > 0) ? ((delta > 8) ? 8 : 2) : ((delta < -8) ? -8 : -2);
  _bl_cur = (uint8_t)((int)_bl_cur + step);
  _dsp->setBacklightDuty(_bl_cur);
  evt_blpwm_(_bl_cur);
}

// --- Events ------------------------------------------------------------------

void PowerService::evt_mode_(const char* m) {
  if (!_api) return;
  _api->publishEvent("power/mode_changed", { {"mode", m} });
}
void PowerService::evt_rails_(const char* what, const char* val) {
  if (!_api) return;
  _api->publishEvent("power/rails", { {"rail", what}, {"state", val} });
}
void PowerService::evt_panel_(const char* what) {
  if (!_api) return;
  _api->publishEvent("power/panel", { {"state", what} });
}
void PowerService::evt_blpwm_(uint8_t duty) {
  if (!_api) return;
  _api->publishEvent("power/bl_pwm", { {"duty", String((int)duty)} });
}
