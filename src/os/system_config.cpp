#include <Arduino.h>
#include <FS.h>
#include "os/system_config.hpp"
#include "os/ini_parser.hpp"

// kleine Helfer
static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// --- Klassen-Methode passend zur Deklaration im Header ----------------------
uint16_t SystemConfig::parseHHMM(const String &s) {
  int sep = s.indexOf(':');
  if (sep < 0) return 0;
  int hh = s.substring(0, sep).toInt();
  int mm = s.substring(sep + 1).toInt();
  hh = constrain(hh, 0, 23);
  mm = constrain(mm, 0, 59);
  return (uint16_t)(hh * 60 + mm);
}

static uint16_t parseHHMM(const String &s) {
  int sep = s.indexOf(':');
  if (sep < 0) return 0;
  int hh = s.substring(0, sep).toInt();
  int mm = s.substring(sep + 1).toInt();
  hh = constrain(hh, 0, 23);
  mm = constrain(mm, 0, 59);
  return (uint16_t)(hh * 60 + mm);
}

static String mmToHHMM(uint16_t m) {
  char buf[6];
  uint8_t hh = m / 60, mm = m % 60;
  snprintf(buf, sizeof(buf), "%02u:%02u", hh, mm);
  return String(buf);
}

// -----------------------------------------------------------------------------
// SystemConfig
// -----------------------------------------------------------------------------

bool SystemConfig::load(fs::FS &fs, const char *path) {
  IniFile ini;

  // Falls Datei fehlt: Defaults schreiben und danach laden
  if (!ini.load(fs, path)) {
    String def = makeDefaultIni();
    File wf = fs.open(path, "w");
    if (wf) { wf.print(def); wf.close(); }
    (void)ini.load(fs, path); // nochmal versuchen; bei Fehler bleiben Defaults aktiv
  }

  // [meta]
  power_profile = ini.get("meta", "profile", power_profile);

  // [display]
  display.brightness_min = (uint8_t)ini.getInt("display", "brightness_min", display.brightness_min);
  display.brightness_max = (uint8_t)ini.getInt("display", "brightness_max", display.brightness_max);
  display.timeout_ready_s = (uint16_t)ini.getInt("display", "timeout_ready_s", display.timeout_ready_s);
  display.timeout_standby_to_lightsleep_s =
      (uint16_t)ini.getInt("display", "timeout_standby_to_lightsleep_s", display.timeout_standby_to_lightsleep_s);
  if (display.brightness_min > display.brightness_max)
    display.brightness_min = display.brightness_max;

  // [wakeup]
  wake_button_short = ini.get("wakeup", "button_short", wake_button_short);
  wake_touch        = ini.getBool("wakeup", "touch",       wake_touch);
  wake_motion       = ini.getBool("wakeup", "motion",      wake_motion);
  wake_radio_event  = ini.getBool("wakeup", "radio_event", wake_radio_event);

  // [quiet]
  quiet_enable = ini.getBool("quiet", "enable", quiet_enable);
  {
    String s = ini.get("quiet", "start", "23:00");
    quiet_start_min = parseHHMM(s);
    s = ini.get("quiet", "end", "07:00");
    quiet_end_min = parseHHMM(s);
  }
  quiet_screen_on_on_event = ini.getBool("quiet", "screen_on_on_event", quiet_screen_on_on_event);
  quiet_haptics            = ini.getBool("quiet", "haptics",             quiet_haptics);
  quiet_bl_cap_pct         = (uint8_t)ini.getInt("quiet", "bl_cap_pct",  quiet_bl_cap_pct);

  // [radio]
  radio_ble      = ini.get("radio", "ble",  radio_ble);
  radio_wifi     = ini.get("radio", "wifi", radio_wifi);
  lora_rx_policy = ini.get("radio", "lora_rx_policy", lora_rx_policy);
  lora_period_s  = (uint16_t)ini.getInt("radio", "lora_period_s", lora_period_s);

  // [charger]
  charger_mode = ini.get("charger", "mode", charger_mode);

  // [pmu]
  pmu.charge_target_mV = (uint16_t)ini.getInt("pmu", "charge_target_mV", pmu.charge_target_mV);
  pmu.vbus_limit_mA    = (uint16_t)ini.getInt("pmu", "vbus_limit_mA",    pmu.vbus_limit_mA);

  return true;
}

bool SystemConfig::save(fs::FS &fs, const char *path) const {
  IniFile ini;

  // [meta]
  ini.set("meta", "profile", power_profile);

  // [display]
  ini.setInt("display", "brightness_min", display.brightness_min);
  ini.setInt("display", "brightness_max", display.brightness_max);
  ini.setInt("display", "timeout_ready_s", display.timeout_ready_s);
  ini.setInt("display", "timeout_standby_to_lightsleep_s", display.timeout_standby_to_lightsleep_s);

  // [wakeup]
  ini.set("wakeup", "button_short", wake_button_short);
  ini.setBool("wakeup", "touch",       wake_touch);
  ini.setBool("wakeup", "motion",      wake_motion);
  ini.setBool("wakeup", "radio_event", wake_radio_event);

  // [quiet]
  ini.setBool("quiet", "enable", quiet_enable);
  ini.set("quiet", "start", mmToHHMM(quiet_start_min));
  ini.set("quiet", "end",   mmToHHMM(quiet_end_min));
  ini.setBool("quiet", "screen_on_on_event", quiet_screen_on_on_event);
  ini.setBool("quiet", "haptics",            quiet_haptics);
  ini.setInt("quiet", "bl_cap_pct",          quiet_bl_cap_pct);

  // [radio]
  ini.set("radio", "ble",            radio_ble);
  ini.set("radio", "wifi",           radio_wifi);
  ini.set("radio", "lora_rx_policy", lora_rx_policy);
  ini.setInt("radio", "lora_period_s", lora_period_s);

  // [charger]
  ini.set("charger", "mode", charger_mode);

  // [pmu]
  ini.setInt("pmu", "charge_target_mV", pmu.charge_target_mV);
  ini.setInt("pmu", "vbus_limit_mA",    pmu.vbus_limit_mA);

  String header = "T-WatchOS+ system.conf (INI)";
  return ini.save(fs, path, header);
}

String SystemConfig::makeDefaultIni() {
  String s;
  s += "; T-WatchOS+ system.conf (INI)\n";
  s += "[meta]\n";
  s += "profile=balanced\n\n";

  s += "[display]\n";
  s += "brightness_min=60\n";
  s += "brightness_max=220\n";
  s += "timeout_ready_s=20\n";
  s += "timeout_standby_to_lightsleep_s=45\n\n";

  s += "[wakeup]\n";
  s += "button_short=toggle_ready_standby\n";
  s += "touch=1\n";
  s += "motion=0\n";
  s += "radio_event=1\n\n";

  s += "[quiet]\n";
  s += "enable=0\n";
  s += "start=23:00\n";
  s += "end=07:00\n";
  s += "screen_on_on_event=0\n";
  s += "haptics=0\n";
  s += "bl_cap_pct=60\n\n";

  s += "[radio]\n";
  s += "ble=auto\n";
  s += "wifi=off\n";
  s += "lora_rx_policy=periodic\n";
  s += "lora_period_s=60\n\n";

  s += "[charger]\n";
  s += "mode=auto\n\n";

  s += "[pmu]\n";
  s += "charge_target_mV=4320\n";
  s += "vbus_limit_mA=500\n";
  return s;
}

// -----------------------------------------------------------------------------
// DevConfig
// -----------------------------------------------------------------------------

bool DevConfig::load(fs::FS &fs, const char *path) {
  IniFile ini;

  if (!ini.load(fs, path)) {
    String def = makeDefaultIni();
    File wf = fs.open(path, "w");
    if (wf) { wf.print(def); wf.close(); }
    (void)ini.load(fs, path);
  }

  // [meta]
  unit_id = ini.get("meta", "unit_id", unit_id);
  hw_rev  = (uint8_t)ini.getInt("meta", "hw_rev", hw_rev);

  // [pmu_limits]
  pmu_limits.charge_target_mV_min = (uint16_t)ini.getInt("pmu_limits","charge_target_mV_min", pmu_limits.charge_target_mV_min);
  pmu_limits.charge_target_mV_max = (uint16_t)ini.getInt("pmu_limits","charge_target_mV_max", pmu_limits.charge_target_mV_max);
  pmu_limits.vbus_limit_mA_min    = (uint16_t)ini.getInt("pmu_limits","vbus_limit_mA_min",    pmu_limits.vbus_limit_mA_min);
  pmu_limits.vbus_limit_mA_max    = (uint16_t)ini.getInt("pmu_limits","vbus_limit_mA_max",    pmu_limits.vbus_limit_mA_max);

  // [pmu_defaults]
  pmu_defaults.charge_target_mV = (uint16_t)ini.getInt("pmu_defaults","charge_target_mV", pmu_defaults.charge_target_mV);
  pmu_defaults.vbus_limit_mA    = (uint16_t)ini.getInt("pmu_defaults","vbus_limit_mA",    pmu_defaults.vbus_limit_mA);

  // [rails]
  rails.backlight_mV = (uint16_t)ini.getInt("rails","backlight_mV", rails.backlight_mV);
  rails.lora_vdd_mV  = (uint16_t)ini.getInt("rails","lora_vdd_mV",  rails.lora_vdd_mV);
  rails.lora_pa_mV   = (uint16_t)ini.getInt("rails","lora_pa_mV",   rails.lora_pa_mV);
  rails.vibra_mV     = (uint16_t)ini.getInt("rails","vibra_mV",     rails.vibra_mV);

  // [display_dev]
  display_dev.bl_ledc_freq = (uint32_t)ini.getInt("display_dev","bl_ledc_freq", display_dev.bl_ledc_freq);
  display_dev.bl_ledc_bits = (uint8_t) ini.getInt("display_dev","bl_ledc_bits", display_dev.bl_ledc_bits);

  // [power_dev]
  power_dev.min_awake_ms = (uint32_t)ini.getInt("power_dev","min_awake_ms", power_dev.min_awake_ms);

  // [locks]
  lock_power_profile = ini.get("locks","power_profile", lock_power_profile);

  // [debug]
  debug_avoid_ls_when_usb = ini.getBool("debug","avoid_ls_when_usb", debug_avoid_ls_when_usb);

  return true;
}

bool DevConfig::save(fs::FS &fs, const char *path) const {
  IniFile ini;

  // [meta]
  ini.set("meta","unit_id", unit_id);
  ini.setInt("meta","hw_rev", hw_rev);

  // [pmu_limits]
  ini.setInt("pmu_limits","charge_target_mV_min", pmu_limits.charge_target_mV_min);
  ini.setInt("pmu_limits","charge_target_mV_max", pmu_limits.charge_target_mV_max);
  ini.setInt("pmu_limits","vbus_limit_mA_min",    pmu_limits.vbus_limit_mA_min);
  ini.setInt("pmu_limits","vbus_limit_mA_max",    pmu_limits.vbus_limit_mA_max);

  // [pmu_defaults]
  ini.setInt("pmu_defaults","charge_target_mV", pmu_defaults.charge_target_mV);
  ini.setInt("pmu_defaults","vbus_limit_mA",    pmu_defaults.vbus_limit_mA);

  // [rails]
  ini.setInt("rails","backlight_mV", rails.backlight_mV);
  ini.setInt("rails","lora_vdd_mV",  rails.lora_vdd_mV);
  ini.setInt("rails","lora_pa_mV",   rails.lora_pa_mV);
  ini.setInt("rails","vibra_mV",     rails.vibra_mV);

  // [display_dev]
  ini.setInt("display_dev","bl_ledc_freq", display_dev.bl_ledc_freq);
  ini.setInt("display_dev","bl_ledc_bits", display_dev.bl_ledc_bits);

  // [power_dev]
  ini.setInt("power_dev","min_awake_ms", power_dev.min_awake_ms);

  // [locks]
  if (lock_power_profile.length())
    ini.set("locks","power_profile", lock_power_profile);

  // [debug]
  ini.setBool("debug","avoid_ls_when_usb", debug_avoid_ls_when_usb);

  String header = "T-WatchOS+ dev.conf (INI)";
  return ini.save(fs, path, header);
}

String DevConfig::makeDefaultIni() {
  String s;
  s += "; T-WatchOS+ dev.conf (INI)\n";
  s += "[meta]\n";
  s += "unit_id=TWATCH-S3-0001\n";
  s += "hw_rev=1\n\n";

  s += "[pmu_limits]\n";
  s += "charge_target_mV_min=4100\n";
  s += "charge_target_mV_max=4400\n";
  s += "vbus_limit_mA_min=100\n";
  s += "vbus_limit_mA_max=500\n\n";

  s += "[pmu_defaults]\n";
  s += "charge_target_mV=4320\n";
  s += "vbus_limit_mA=500\n\n";

  s += "[rails]\n";
  s += "backlight_mV=3300\n";
  s += "lora_vdd_mV=3300\n";
  s += "lora_pa_mV=3300\n";
  s += "vibra_mV=3000\n\n";

  s += "[display_dev]\n";
  s += "bl_ledc_freq=1000\n";
  s += "bl_ledc_bits=8\n\n";

  s += "[power_dev]\n";
  s += "min_awake_ms=3000\n\n";

  s += "[locks]\n";
  s += ";power_profile=balanced\n\n";

  s += "[debug]\n";
  s += "avoid_ls_when_usb=1\n";
  return s;
}
