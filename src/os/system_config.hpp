#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include "os/ini_parser.hpp"

// Pfade
constexpr const char* USER_CFG_PATH = "/data/config/system.conf";
constexpr const char* DEV_CFG_PATH  = "/data/config/dev.conf";

struct SystemConfig {
  // ---- meta/user profile ---------------------------------------------------
  String power_profile = "balanced"; // balanced|performance|endurance

  // ---- display/user timeouts ----------------------------------------------
  struct Display {
    uint8_t brightness_min = 60;    // 0..255
    uint8_t brightness_max = 220;   // 0..255
    uint16_t timeout_ready_s = 20;
    uint16_t timeout_standby_to_lightsleep_s = 45;
  } display;

  // ---- wake/user -----------------------------------------------------------
  String  wake_button_short = "toggle_ready_standby"; // or "none"
  bool    wake_touch        = true;
  bool    wake_motion       = false;
  bool    wake_radio_event  = true;

  // ---- quiet/user ----------------------------------------------------------
  bool     quiet_enable = false;
  uint16_t quiet_start_min = 23*60; // minutes since midnight
  uint16_t quiet_end_min   = 7*60;
  bool     quiet_screen_on_on_event = false;
  bool     quiet_haptics            = false;
  uint8_t  quiet_bl_cap_pct         = 60;  // 10..100

  // ---- radio/user ----------------------------------------------------------
  String  radio_ble  = "auto";      // off|on|auto
  String  radio_wifi = "off";       // off|on|auto
  String  lora_rx_policy = "periodic"; // off|periodic|always
  uint16_t lora_period_s = 60;

  // ---- charger/user --------------------------------------------------------
  String  charger_mode = "auto";    // auto|never

  // ---- pmu/user (defaults; dev clamps these) ------------------------------
  struct PMUUser {
    uint16_t charge_target_mV = 4320;
    uint16_t vbus_limit_mA    = 500;
  } pmu;

  // API
  bool load(fs::FS& fs, const char* path);
  bool save(fs::FS& fs, const char* path) const;

  static String makeDefaultIni();
  static uint16_t parseHHMM(const String& hhmm);
};

struct DevConfig {
  // meta
  String unit_id = "TWATCH-S3-0001";
  int    hw_rev  = 1;

  // hard limits (clamp)
  struct PMULimits {
    uint16_t charge_target_mV_min = 4100;
    uint16_t charge_target_mV_max = 4400;
    uint16_t vbus_limit_mA_min    = 100;
    uint16_t vbus_limit_mA_max    = 500;
  } pmu_limits;

  // pmu defaults (used at first boot etc.)
  struct PMUDefaults {
    uint16_t charge_target_mV = 4320;
    uint16_t vbus_limit_mA    = 500;
  } pmu_defaults;

  // rails
  struct Rails {
    uint16_t backlight_mV = 3300;
    uint16_t lora_vdd_mV  = 3300;
    uint16_t lora_pa_mV   = 3300;
    uint16_t vibra_mV     = 3000;
  } rails;

  // display dev
  struct DisplayDev {
    uint32_t bl_ledc_freq = 1000; // Hz
    uint8_t  bl_ledc_bits = 8;
  } display_dev;

  // power dev
  struct PowerDev {
    uint32_t min_awake_ms = 3000;
  } power_dev;

  // locks
  String lock_power_profile = ""; // "", "balanced", "performance", "endurance"

  // debug/dev
  bool debug_avoid_ls_when_usb = true;

  // API
  bool load(fs::FS& fs, const char* path);
  bool save(fs::FS& fs, const char* path) const;

  static String makeDefaultIni();
};

// clamp + locks
inline void clampWithDev(SystemConfig& u, const DevConfig& d) {
  // PMU clamp
  u.pmu.charge_target_mV = (uint16_t)constrain((int)u.pmu.charge_target_mV,
                                               (int)d.pmu_limits.charge_target_mV_min,
                                               (int)d.pmu_limits.charge_target_mV_max);
  u.pmu.vbus_limit_mA = (uint16_t)constrain((int)u.pmu.vbus_limit_mA,
                                            (int)d.pmu_limits.vbus_limit_mA_min,
                                            (int)d.pmu_limits.vbus_limit_mA_max);
  // profile lock
  if (d.lock_power_profile.length() > 0) {
    u.power_profile = d.lock_power_profile;
  }

  // quiet cap clamp
  u.quiet_bl_cap_pct = (uint8_t)constrain((int)u.quiet_bl_cap_pct, 10, 100);

  // brightness clamp to 0..255, and ensure min<=max
  u.display.brightness_min = (uint8_t)constrain(u.display.brightness_min, 0, 255);
  u.display.brightness_max = (uint8_t)constrain(u.display.brightness_max, 0, 255);
  if (u.display.brightness_min > u.display.brightness_max) {
    u.display.brightness_min = u.display.brightness_max;
  }
}
