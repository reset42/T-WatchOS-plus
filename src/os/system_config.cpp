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

// --- Klassen-Methoden passend zur Deklaration im Header ----------------------
uint16_t SystemConfig::parseHHMM(const String &s) {
  int sep = s.indexOf(':');
  if (sep < 0) return 0;
  uint16_t hh = 0, mm = 0;
  for (int i=0;i<sep;i++) {
    char c = s[i];
    if (c<'0'||c>'9') { hh=0; break; }
    hh = hh*10 + (c-'0');
  }
  for (int i=sep+1;i<(int)s.length();i++) {
    char c = s[i];
    if (c<'0'||c>'9') { break; }
    mm = mm*10 + (c-'0');
  }
  hh = constrain(hh, 0, 23);
  mm = constrain(mm, 0, 59);
  return (uint16_t)(hh * 60 + mm);
}

String SystemConfig::mmToHHMM(uint16_t m) {
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
    String def = SystemConfig::makeDefaultIni();
    File wf = fs.open(path, "w");
    if (wf) { wf.print(def); wf.close(); }
    (void)ini.load(fs, path); // nochmal versuchen; bei Fehler bleiben Defaults aktiv
  }

  // [meta]
  power_profile = ini.get("meta", "profile", power_profile);

  // [display]
  display.brightness_min  = (uint8_t) ini.getInt("display", "brightness_min",  display.brightness_min);
  display.brightness_max  = (uint8_t) ini.getInt("display", "brightness_max",  display.brightness_max);
  display.timeout_ready_s = (uint16_t)ini.getInt("display", "timeout_ready_s", display.timeout_ready_s);
  display.timeout_standby_to_lightsleep_s =
      (uint16_t)ini.getInt("display", "timeout_standby_to_lightsleep_s",
                           display.timeout_standby_to_lightsleep_s);

  // [wake]
  wake_button_short = ini.get("wake", "button_short", wake_button_short);
  wake_touch        = ini.getBool("wake", "touch",      wake_touch);
  wake_motion       = ini.getBool("wake", "motion",     wake_motion);
  wake_radio_event  = ini.getBool("wake", "radio_event",wake_radio_event);

  // [quiet]
  quiet_enable              = ini.getBool("quiet", "enable",               quiet_enable);
  quiet_start_min           = (uint16_t)ini.getInt("quiet", "start_min",   quiet_start_min);
  quiet_end_min             = (uint16_t)ini.getInt("quiet", "end_min",     quiet_end_min);
  quiet_screen_on_on_event  = ini.getBool("quiet", "screen_on_on_event",   quiet_screen_on_on_event);
  quiet_haptics             = ini.getBool("quiet", "haptics",              quiet_haptics);
  quiet_bl_cap_pct          = (uint8_t) ini.getInt("quiet", "bl_cap_pct",  quiet_bl_cap_pct);

  // [radio] (neu + Legacy-Keys)
  radio.allow_rx_wake = ini.getBool("radio", "allow_rx_wake", radio.allow_rx_wake);

  // Legacy/Compat: radio_ble / radio_wifi / lora_rx_policy / lora_period_s
  radio_ble      = ini.get("radio", "ble",  radio_ble);
  radio_wifi     = ini.get("radio", "wifi", radio_wifi);
  lora_rx_policy = ini.get("radio", "lora_rx_policy",
                           lora_rx_policy.length() ? lora_rx_policy : radio.lora_policy);

  // Akzeptiere alten Key "lora_policy" als Fallback
  if (lora_rx_policy.length() == 0) {
    lora_rx_policy = ini.get("radio", "lora_policy", radio.lora_policy);
  }

  lora_period_s  = (uint16_t)ini.getInt("radio", "lora_period_s",
                                        lora_period_s ? lora_period_s : radio.lora_period_s);

  // Synchronisiere in die neue Struktur
  radio.lora_policy  = lora_rx_policy;
  radio.lora_period_s = lora_period_s;

  // [pmu] (für charger.mode)
  charger_mode = ini.get("pmu", "charger_mode", charger_mode);

  return true;
}

bool SystemConfig::save(fs::FS &fs, const char *path) const {
  IniFile ini;

  // [meta]
  ini.set("meta","profile", power_profile);

  // [display]
  ini.setInt("display","brightness_min", display.brightness_min);
  ini.setInt("display","brightness_max", display.brightness_max);
  ini.setInt("display","timeout_ready_s", display.timeout_ready_s);
  ini.setInt("display","timeout_standby_to_lightsleep_s", display.timeout_standby_to_lightsleep_s);

  // [wake]
  ini.set("wake","button_short", wake_button_short);
  ini.setBool("wake","touch",      wake_touch);
  ini.setBool("wake","motion",     wake_motion);
  ini.setBool("wake","radio_event",wake_radio_event);

  // [quiet]
  ini.setBool("quiet","enable", quiet_enable);
  ini.setInt ("quiet","start_min", quiet_start_min);
  ini.setInt ("quiet","end_min", quiet_end_min);
  ini.setBool("quiet","screen_on_on_event", quiet_screen_on_on_event);
  ini.setBool("quiet","haptics", quiet_haptics);
  ini.setInt ("quiet","bl_cap_pct", quiet_bl_cap_pct);

  // [radio] – Legacy-kompatibel + neue Struktur
  ini.setBool("radio","allow_rx_wake", radio.allow_rx_wake);
  ini.set("radio","ble",  radio_ble);
  ini.set("radio","wifi", radio_wifi);
  ini.set("radio","lora_rx_policy", lora_rx_policy.length() ? lora_rx_policy : radio.lora_policy);
  ini.setInt("radio","lora_period_s", (int)(lora_period_s ? lora_period_s : radio.lora_period_s));
  // Schreibe zusätzlich den alten Key für Abwärtskompatibilität
  ini.set("radio","lora_policy", radio.lora_policy);

  // [pmu] – charger.mode
  ini.set("pmu", "charger_mode", charger_mode);

  return ini.save(fs, path);
}

String SystemConfig::makeDefaultIni() {
  String s;
  s.reserve(768);

  s += "[meta]\n";
  s += "profile=balanced\n\n";

  s += "[display]\n";
  s += "brightness_min=60\n";
  s += "brightness_max=255\n";
  s += "timeout_ready_s=20\n";
  s += "timeout_standby_to_lightsleep_s=45\n\n";

  s += "[wake]\n";
  s += "button_short=toggle_ready_standby\n";
  s += "touch=on\n";
  s += "motion=off\n";
  s += "radio_event=on\n\n";

  s += "[quiet]\n";
  s += "enable=off\n";
  s += "start_min=1380\n";
  s += "end_min=420\n";
  s += "screen_on_on_event=off\n";
  s += "haptics=off\n";
  s += "bl_cap_pct=60\n\n";

  s += "[radio]\n";
  s += "allow_rx_wake=on\n";
  s += "ble=auto\n";
  s += "wifi=auto\n";
  s += "lora_rx_policy=auto\n";
  s += "lora_policy=auto\n"; // zusätzlicher Legacy-Key
  s += "lora_period_s=60\n\n";

  s += "[pmu]\n";
  s += "charger_mode=auto\n";

  return s;
}

// -----------------------------------------------------------------------------
// DevConfig
// -----------------------------------------------------------------------------

bool DevConfig::load(fs::FS &fs, const char *path) {
  IniFile ini;

  // Falls Datei fehlt: Defaults schreiben und danach laden
  if (!ini.load(fs, path)) {
    String def = DevConfig::makeDefaultIni();
    File wf = fs.open(path, "w");
    if (wf) { wf.print(def); wf.close(); }
    (void)ini.load(fs, path); // nochmal versuchen
  }

  // [meta]
  unit_id = ini.get("meta","unit_id", unit_id);
  hw_rev  = (int)ini.getInt("meta","hw_rev", hw_rev);

  // [pmu_limits]
  pmu_limits.charge_target_mV_min = (uint16_t)ini.getInt("pmu_limits","charge_target_mV_min", pmu_limits.charge_target_mV_min);
  pmu_limits.charge_target_mV_max = (uint16_t)ini.getInt("pmu_limits","charge_target_mV_max", pmu_limits.charge_target_mV_max);
  pmu_limits.vbus_limit_mA_min    = (uint16_t)ini.getInt("pmu_limits","vbus_limit_mA_min",    pmu_limits.vbus_limit_mA_min);
  pmu_limits.vbus_limit_mA_max    = (uint16_t)ini.getInt("pmu_limits","vbus_limit_mA_max",    pmu_limits.vbus_limit_mA_max);

  // [rails]
  rails.backlight_mV = (uint16_t)ini.getInt("rails","backlight_mV", rails.backlight_mV);
  rails.lora_vdd_mV  = (uint16_t)ini.getInt("rails","lora_vdd_mV",  rails.lora_vdd_mV);
  rails.lora_pa_mV   = (uint16_t)ini.getInt("rails","lora_pa_mV",   rails.lora_pa_mV);
  rails.vibra_mV     = (uint16_t)ini.getInt("rails","vibra_mV",     rails.vibra_mV);

  // [display_dev]
  display_dev.bl_ledc_freq = (uint32_t)ini.getInt("display_dev","bl_ledc_freq", display_dev.bl_ledc_freq);
  display_dev.bl_ledc_bits = (uint8_t) ini.getInt("display_dev","bl_ledc_bits", display_dev.bl_ledc_bits);
  display_dev.bl_ready_duty = (uint8_t)ini.getInt("display_dev","bl_ready_duty", display_dev.bl_ready_duty);
  display_dev.bl_ramp_step  = (uint8_t)ini.getInt("display_dev","bl_ramp_step",  display_dev.bl_ramp_step);
  display_dev.bl_ramp_ms    = (uint16_t)ini.getInt("display_dev","bl_ramp_ms",   display_dev.bl_ramp_ms);

  // [power_dev]
  power_dev.min_awake_ms     = (uint16_t)ini.getInt("power_dev","min_awake_ms",     power_dev.min_awake_ms);
  power_dev.ready_timeout_s  = (uint16_t)ini.getInt("power_dev","ready_timeout_s",  power_dev.ready_timeout_s);
  power_dev.standby_to_ls_s  = (uint16_t)ini.getInt("power_dev","standby_to_ls_s",  power_dev.standby_to_ls_s);

  // [locks]
  locks.power_profile = ini.get("locks","power_profile", locks.power_profile);

  // [debug]
  debug.avoid_ls_when_usb = ini.getBool("debug","avoid_ls_when_usb", debug.avoid_ls_when_usb);
  debug.pmu_events        = ini.getBool("debug","pmu_events",        debug.pmu_events);

  // Legacy-Compat Spiegel
  debug_avoid_ls_when_usb = debug.avoid_ls_when_usb;

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

  // [rails]
  ini.setInt("rails","backlight_mV", rails.backlight_mV);
  ini.setInt("rails","lora_vdd_mV",  rails.lora_vdd_mV);
  ini.setInt("rails","lora_pa_mV",   rails.lora_pa_mV);
  ini.setInt("rails","vibra_mV",     rails.vibra_mV);

  // [display_dev]
  ini.setInt("display_dev","bl_ledc_freq", display_dev.bl_ledc_freq);
  ini.setInt("display_dev","bl_ledc_bits", display_dev.bl_ledc_bits);
  ini.setInt("display_dev","bl_ready_duty", display_dev.bl_ready_duty);
  ini.setInt("display_dev","bl_ramp_step",  display_dev.bl_ramp_step);
  ini.setInt("display_dev","bl_ramp_ms",    display_dev.bl_ramp_ms);

  // [power_dev]
  ini.setInt("power_dev","min_awake_ms",     power_dev.min_awake_ms);
  ini.setInt("power_dev","ready_timeout_s",  power_dev.ready_timeout_s);
  ini.setInt("power_dev","standby_to_ls_s",  power_dev.standby_to_ls_s);

  // [locks]
  ini.set("locks","power_profile", locks.power_profile);

  // [debug]
  // Schreibe den kanonischen Ort
  ini.setBool("debug","avoid_ls_when_usb", debug.avoid_ls_when_usb);
  ini.setBool("debug","pmu_events",        debug.pmu_events);

  return ini.save(fs, path);
}

String DevConfig::makeDefaultIni() {
  String s;
  s.reserve(512);

  s += "[meta]\n";
  s += "unit_id=TWATCH-S3-0001\n";
  s += "hw_rev=1\n\n";

  s += "[pmu_limits]\n";
  s += "charge_target_mV_min=4100\n";
  s += "charge_target_mV_max=4400\n";
  s += "vbus_limit_mA_min=100\n";
  s += "vbus_limit_mA_max=500\n\n";

  s += "[rails]\n";
  s += "backlight_mV=3300\n";
  s += "lora_vdd_mV=3300\n";
  s += "lora_pa_mV=3300\n";
  s += "vibra_mV=3000\n\n";

  s += "[display_dev]\n";
  s += "bl_ledc_freq=1000\n";
  s += "bl_ledc_bits=8\n";
  s += "bl_ready_duty=160\n";
  s += "bl_ramp_step=6\n";
  s += "bl_ramp_ms=6\n\n";

  s += "[power_dev]\n";
  s += "min_awake_ms=3000\n";
  s += "ready_timeout_s=20\n";
  s += "standby_to_ls_s=120\n\n";

  s += "[locks]\n";
  s += "power_profile=\n\n";

  s += "[debug]\n";
  s += "avoid_ls_when_usb=on\n";
  s += "pmu_events=off\n";

  return s;
}
