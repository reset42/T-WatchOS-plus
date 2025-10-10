#include "os/display_service.hpp"
#include "os/system_config.hpp"
#include "drivers/board_pins.hpp"
#include "drivers/dsp_st7789v.hpp"
#include "os/api_bus.hpp"
#include <Arduino.h>
#include <SPI.h>

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void DisplayService::begin(PMU_AXP2101* pmu, const DevConfig& dev, const SystemConfig& /*sys*/) {
  _pmu = pmu;

  // PWM Setup aus DevConfig (Fallbacks)
  _ledc_hz   = dev.display_dev.bl_ledc_freq ? dev.display_dev.bl_ledc_freq : 1000;
  _ledc_bits = dev.display_dev.bl_ledc_bits ? dev.display_dev.bl_ledc_bits : 8;
  _bl_mv     = dev.rails.backlight_mV       ? dev.rails.backlight_mV       : 3300;

  // Optionale zukünftige Konfig-Werte aus dev.conf (wenn vorhanden):
  //  - _ready_duty, _ramp_step, _ramp_ms
  //  (aktuell mit Defaults initialisiert; Parser-Anpassung folgt separat nach Freigabe)

  // BL-GPIO sicher aktivieren, dann LEDC anhängen
  pinMode(TWATCH_S3_TFT_Pins::BL, OUTPUT);
  digitalWrite(TWATCH_S3_TFT_Pins::BL, HIGH);

  ledcSetup(_ledc_ch, _ledc_hz, _ledc_bits);
  ledcAttachPin(TWATCH_S3_TFT_Pins::BL, _ledc_ch);
  setBacklightDuty(0);

  _panel_ready   = false; // echte Panel-Init in onReady() oder beim ersten grid
  _ramp_active   = false;
  _ramp_target   = 0;
  _ramp_next_ms  = 0;
}

void DisplayService::attachApi(ApiBus& api) {
  _api = &api;
  _api->registerHandler("display", [this](const ApiRequest& r) {
    const String& act = r.action;

    if (act.equalsIgnoreCase("grid")) {
      // Failsafe-Init, falls noch nicht erfolgt
      if (!_panel_ready) {
        _dsp.begin(TWATCH_S3_TFT_Pins::MOSI, TWATCH_S3_TFT_Pins::SCLK,
                   TWATCH_S3_TFT_Pins::CS,   TWATCH_S3_TFT_Pins::DC,
                   TWATCH_S3_TFT_Pins::RST,  240, 240);
        _panel_ready = true;
      }
      _dsp.drawTestGrid();
      _api->replyOk(r.origin);
      return;
    }

    if (act.equalsIgnoreCase("bl")) {
      const String* v = ApiBus::findParam(r.params, "duty");
      if (!v) { _api->replyErr(r.origin, "bad_param", "missing duty"); return; }
      uint8_t duty = (uint8_t)constrain(v->toInt(), 0, 255);
      // Direkt setzen (ohne Ramp)
      setBacklightDuty(duty);
      _api->replyOk(r.origin, std::vector<ApiKV>{{"bl", String((int)duty)}});
      return;
    }

    if (act.equalsIgnoreCase("ramp")) {
      const String* v  = ApiBus::findParam(r.params, "duty");
      if (!v) { _api->replyErr(r.origin, "bad_param", "missing duty"); return; }
      const String* st = ApiBus::findParam(r.params, "step");
      const String* ms = ApiBus::findParam(r.params, "ms");
      uint8_t  duty = (uint8_t)constrain(v->toInt(), 0, 255);
      uint8_t  step = st ? (uint8_t)constrain(st->toInt(), 1, 64) : _ramp_step;
      uint16_t spms = ms ? (uint16_t)constrain(ms->toInt(), 1, 50) : _ramp_ms;
      setBacklightDutySmooth(duty, step, spms);
      _api->replyOk(r.origin, {{"bl_target", String((int)duty)},
                               {"step",      String((int)step)},
                               {"ms",        String((int)spms)}});
      return;
    }

    _api->replyErr(r.origin, "bad_action", "unknown display action");
  });
}

void DisplayService::onReady() {
  // Panel initialisieren, wenn nötig
  if (!_panel_ready) {
    _dsp.begin(TWATCH_S3_TFT_Pins::MOSI, TWATCH_S3_TFT_Pins::SCLK,
               TWATCH_S3_TFT_Pins::CS,   TWATCH_S3_TFT_Pins::DC,
               TWATCH_S3_TFT_Pins::RST,  240, 240);
    _panel_ready = true;
  }
  // Immer sanft auf Ready-Duty fahren (Policy)
  setBacklightDutySmooth(_ready_duty, _ramp_step, _ramp_ms);
}

void DisplayService::onStandby() {
  setBacklightDutySmooth(0, _ramp_step, _ramp_ms);
}

void DisplayService::onLightSleep() {
  setBacklightDutySmooth(0, _ramp_step, _ramp_ms);
}

void DisplayService::loop() {
  // Ramping: zeitgesteuert, nicht blockierend
  if (_ramp_active) {
    const uint32_t now = millis();
    if ((int32_t)(now - _ramp_next_ms) >= 0) {
      uint8_t cur = _bl_last;
      if (cur == _ramp_target) {
        _ramp_active = false;
        return;
      }
      if (cur < _ramp_target) {
        uint16_t n = (uint16_t)cur + _ramp_step;
        if (n > _ramp_target) n = _ramp_target;
        setBacklightDuty((uint8_t)n);
      } else {
        int16_t n = (int16_t)cur - (int16_t)_ramp_step;
        if (n < (int16_t)_ramp_target) n = _ramp_target;
        setBacklightDuty((uint8_t)n);
      }
      _ramp_next_ms = now + _ramp_ms;
    }
  }
}

void DisplayService::setBacklightDuty(uint8_t duty) {
  if (duty == _bl_last) return;
  const uint32_t maxSteps = (1UL << _ledc_bits) - 1UL;
  const uint32_t val = (uint32_t)duty * maxSteps / 255UL;
  ledcWrite(_ledc_ch, clamp_u32(val, 0, maxSteps));
  _bl_last = duty;
}

void DisplayService::setBacklightDutySmooth(uint8_t target, uint8_t step, uint16_t ms) {
  // Sofort starten (auch wenn target==_bl_last, es endet nach erstem Check)
  _ramp_target  = target;
  _ramp_step    = step ? step : 1;
  _ramp_ms      = ms   ? ms   : 1;
  _ramp_active  = true;
  _ramp_next_ms = millis(); // tickt im nächsten loop()
}

void DisplayService::setBacklightRail(uint16_t mv, bool /*enable*/) {
  // Ownership-Regel: PowerService schaltet Rail; hier nur Zielspannung merken
  _bl_mv = mv;
}
