#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>

// Default paths for configs on LittleFS
constexpr const char* USER_CFG_PATH = "/config/system.conf";
constexpr const char* DEV_CFG_PATH  = "/config/dev.conf";

// ---------------- DEV CONFIG -------------------------------------------------
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

  // rails
  struct Rails {
    uint16_t backlight_mV = 3300;
    uint16_t lora_vdd_mV  = 3300;
    uint16_t lora_pa_mV   = 3300;
    uint16_t vibra_mV     = 3000;
  } rails;

  // display device
  struct DisplayDev {
    uint32_t bl_ledc_freq = 1000; // Hz
    uint8_t  bl_ledc_bits = 8;
    // Backlight-Defaults
    uint8_t  bl_ready_duty = 160;   // 0..255
    uint8_t  bl_ramp_step  = 6;     // 1..64
    uint16_t bl_ramp_ms    = 6;     // ms per step
  } display_dev;

  // power device (timings etc.)
  struct PowerDev {
    uint16_t min_awake_ms    = 3000;
    uint16_t ready_timeout_s = 20;
    uint16_t standby_to_ls_s = 120;
  } power_dev;

  // locks (optional erzwungene Defaults)
  struct Locks {
    String   power_profile = ""; // "", "balanced", "performance", "endurance"
  } locks;

  // debug/dev-flags
  struct Debug {
    bool avoid_ls_when_usb = true;
    bool pmu_events        = false;
  } debug;

  // ---- Legacy-Compat (für vorhandene Call-Sites) ---------------------------
  // Spiegel von debug.avoid_ls_when_usb (wird in load/save synchronisiert)
  bool debug_avoid_ls_when_usb = true;

  // ---- API -----------------------------------------------------------------
  bool   load(fs::FS& fs, const char* path = DEV_CFG_PATH);
  bool   save(fs::FS& fs, const char* path = DEV_CFG_PATH) const;
  static String makeDefaultIni();
};

// ---------------- USER SYSTEM CONFIG ----------------------------------------
struct SystemConfig {
  // ---- meta/user profile ---------------------------------------------------
  String power_profile = "balanced"; // balanced|performance|endurance

  // ---- display/user timeouts ----------------------------------------------
  struct Display {
    uint8_t  brightness_min = 0;    // clamp 0..255
    uint8_t  brightness_max = 255;  // clamp 0..255
    uint16_t timeout_ready_s = 20;  // screen-off to standby
    uint16_t timeout_standby_to_lightsleep_s = 45; // standby -> light sleep
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

  // ---- radio/user (neue Struktur) ------------------------------------------
  struct Radio {
    bool    allow_rx_wake = true;   // wake on LoRa/BLE event
    String  lora_policy   = "auto"; // auto|never|...
    uint16_t lora_period_s = 60;
  } radio;

  // ---- Legacy-Compat-Felder (von vorhandenen Call-Sites genutzt) -----------
  String   radio_ble       = "auto";   // erwartet von PowerService/PowerAPI
  String   radio_wifi      = "auto";   // dito
  String   lora_rx_policy  = "auto";   // dito (entspricht radio.lora_policy)
  uint16_t lora_period_s   = 60;       // dito (entspricht radio.lora_period_s)
  String   charger_mode    = "auto";   // erwartet von PowerAPI ("charger.mode")

  // ---- pmu/user (defaults; dev clamps these) ------------------------------
  struct PMUUser {
    uint16_t charge_target_mV = 4320;
    uint16_t vbus_limit_mA    = 500;
  } pmu;

  // Dev-Kopie (einige Services erwarten cfg.dev.*)
  DevConfig dev; // copy of dev for services, kept in sync by main()

  // API
  bool load(fs::FS& fs, const char* path = USER_CFG_PATH);
  bool save(fs::FS& fs, const char* path = USER_CFG_PATH) const;

  // utils
  static String   mmToHHMM(uint16_t mm);
  static uint16_t parseHHMM(const String& hhmm);

  // *** Deklaration ergänzt ***
  static String   makeDefaultIni();
};

// ---------------- clamp & locks helpers -------------------------------------
inline void clampWithDev(SystemConfig& u, const DevConfig& d) {
  // PMU clamp
  u.pmu.charge_target_mV = constrain(u.pmu.charge_target_mV,
                                     d.pmu_limits.charge_target_mV_min,
                                     d.pmu_limits.charge_target_mV_max);
  u.pmu.vbus_limit_mA    = constrain(u.pmu.vbus_limit_mA,
                                     d.pmu_limits.vbus_limit_mA_min,
                                     d.pmu_limits.vbus_limit_mA_max);

  // Locks?
  const String& lock_prof = d.locks.power_profile;
  if (lock_prof.length()) {
    u.power_profile = lock_prof;
  }

  // quiet cap clamp
  if (u.quiet_bl_cap_pct < 10)  u.quiet_bl_cap_pct = 10;
  if (u.quiet_bl_cap_pct > 100) u.quiet_bl_cap_pct = 100;

  // brightness clamp to 0..255, ensure min<=max
  if (u.display.brightness_min > 255) u.display.brightness_min = 255;
  if (u.display.brightness_max > 255) u.display.brightness_max = 255;
  if (u.display.brightness_min > u.display.brightness_max) {
    u.display.brightness_min = u.display.brightness_max;
  }

  // *** Sync neue <-> Legacy Radio-Felder ***
  if (u.lora_rx_policy.length() == 0) u.lora_rx_policy = u.radio.lora_policy;
  else u.radio.lora_policy = u.lora_rx_policy;
  u.lora_period_s = u.radio.lora_period_s;
}
