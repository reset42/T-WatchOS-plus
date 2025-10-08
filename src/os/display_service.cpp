#include "os/display_service.hpp"
#include "drivers/board_pins.hpp"
#include "driver/ledc.h"

void DisplayService::begin(PMU_AXP2101* pmu, DevConfig const& dev, SystemConfig const& sys) {
  _pmu = pmu;

  // Dev-Parameter übernehmen
  _ledc_hz   = dev.display_dev.bl_ledc_freq;
  _ledc_bits = dev.display_dev.bl_ledc_bits;
  _bl_mv     = dev.rails.backlight_mV;

  // Backlight-Rail: vorwärmen für instant-on
  if (_pmu) _pmu->setBacklightRail(_bl_mv, true);

  // LEDC initialisieren
  ledcSetup(_ledc_ch, _ledc_hz, _ledc_bits);
  ledcAttachPin(TWATCH_S3_TFT_Pins::BL, _ledc_ch);
  ledcWrite(_ledc_ch, 0);
  _bl_last = 0;

  // (optional) später: Display-Panel init (über DSP_ST7789V)
  (void)sys;
}

void DisplayService::setBacklightDuty(uint8_t duty) {
  if (duty == _bl_last) return;
  ledcWrite(_ledc_ch, duty);
  _bl_last = duty;
}

void DisplayService::setBacklightRail(uint16_t mv, bool enable) {
  _bl_mv = mv;
  if (_pmu) _pmu->setBacklightRail(_bl_mv, enable);
}

void DisplayService::onReady()     { /* später: Panel aus Sleep holen */ }
void DisplayService::onStandby()   { /* später: ggf. Panel DisplayOff  */ }
void DisplayService::onLightSleep(){ /* später: Deep-Panel-Sleep        */ }
