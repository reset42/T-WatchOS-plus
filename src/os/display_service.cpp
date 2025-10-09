#include "os/display_service.hpp"
#include "os/system_config.hpp"
#include "drivers/board_pins.hpp"
#include "drivers/dsp_st7789v.hpp"
#include "os/bus_guard.hpp"
#include "os/api_bus.hpp"
#include <Arduino.h>
#include <SPI.h>

// Lokale Helfer
static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// -----------------------------------------------------------------------------

void DisplayService::begin(PMU_AXP2101* /*pmu*/, DevConfig const& dev, SystemConfig const& /*sys*/) {
  // DisplayService berührt keine PMU/Rails mehr – nur PWM & SPI-Panel
  _ledc_hz   = dev.display_dev.bl_ledc_freq;
  _ledc_bits = dev.display_dev.bl_ledc_bits;
  _bl_mv     = dev.rails.backlight_mV;   // nur merken; Schalten übernimmt PowerService
  _bl_last   = 0;

  // Backlight-PWM vorbereiten (Duty = 0)
  ledcSetup(_ledc_ch, _ledc_hz, _ledc_bits);
  ledcAttachPin(TWATCH_S3_TFT_Pins::BL, _ledc_ch);
  ledcWrite(_ledc_ch, 0);

  // Panel-Init über SPI Guard
  if (g_bus.lockSPILcdOwner("display", pdMS_TO_TICKS(100))) {
    _dsp.init();
    _dsp.setRotation(0);
    g_bus.unlockSPILcdOwner("display");
  } else {
    Serial.println("[E] display.begin: spi_lcd busy");
  }
}

void DisplayService::attachApi(ApiBus& api) {
  api.registerHandler("display", [this, &api](const ApiRequest& r) {
    const String& act = r.action;

    if (act == "grid") {
      if (g_bus.lockSPILcdOwner("display", pdMS_TO_TICKS(50))) {
        _dsp.drawTestGrid();
        g_bus.unlockSPILcdOwner("display");
      } else {
        Serial.println("[E] display.grid: spi_lcd busy");
      }
      api.replyOk(r.origin, { {"op","grid"} });
      return;
    }

    if (act == "bl") {
      const String* pduty = ApiBus::findParam(r.params, "duty");
      if (!pduty) { api.replyErr(r.origin, "param", "missing duty=0..255"); return; }
      long v = pduty->toInt(); if (v < 0) v = 0; if (v > 255) v = 255;
      setBacklightDuty((uint8_t)v);
      api.replyOk(r.origin, { {"op","bl"}, {"duty", String((int)v)} });
      return;
    }

    api.replyErr(r.origin, "unknown", "display.<grid|bl duty=>");
  });
}

void DisplayService::loop() {
  // aktuell keine zyklischen Aufgaben
}

void DisplayService::onReady() {
  // Panel aus Sleep holen (wenn dein ST7789-Treiber das automatisch in init() tut, ist das hier no-op)
  if (g_bus.lockSPILcdOwner("display", pdMS_TO_TICKS(50))) {
    _dsp.wakeIfSupported(); // idempotent im Treiber implementiert (kein Muss)
    g_bus.unlockSPILcdOwner("display");
  }
}

void DisplayService::onStandby() {
  // Panel in Sleep schicken (idempotent, falls vom Treiber unterstützt)
  if (g_bus.lockSPILcdOwner("display", pdMS_TO_TICKS(50))) {
    _dsp.sleepIfSupported(); // falls nicht vorhanden, bleibt das ein no-op
    g_bus.unlockSPILcdOwner("display");
  }
}

void DisplayService::onLightSleep() {
  // nichts Spezielles – Panel bleibt im Standby-Pfad behandelt
}

void DisplayService::setBacklightDuty(uint8_t duty) {
  if (duty == _bl_last) return;
  const uint32_t maxSteps = (1UL << _ledc_bits) - 1UL;
  const uint32_t val = (uint32_t)duty * maxSteps / 255UL;
  ledcWrite(_ledc_ch, clamp_u32(val, 0, maxSteps));
  _bl_last = duty;
}

void DisplayService::setBacklightRail(uint16_t mv, bool enable) {
  // Seit „Rail-Ownership“ schaltet DisplayService keine Rails mehr.
  // Wir merken die Ziel-mV, tatsächliches Schalten erfolgt im PowerService.
  (void)enable;
  _bl_mv = mv;
}
