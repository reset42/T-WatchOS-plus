// service_power.cpp
// - Keine HW-Details (Pins/Adressen) → alles im Treiber
// - Autowake + LightSleep (persistenter Resume-Beweis)
// - Backlight-Flicker-Fix (dim→sleep→restore via Bus)
// - Guards: prevent_lightsleep / prevent_standby
// - On-Demand Dump:  do power.resume.dump
// - Admin AXP-IRQ:   emit power.axp.irq op=enable_all|clear_all|dump [value=on|off]
// - Telemetrie + Rotation-Log in LittleFS

#include "service_power.hpp"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>

#include "../core/bus.hpp"
#include "../drivers/drv_power_axp2101.hpp"

#include "esp_sleep.h"
#include "esp_err.h"

using drv::axp2101::Axp2101;

// -------------------- Persistentes Logging -----------------------------------
namespace {
  static File   s_log;
  static size_t s_log_limit_bytes = 64 * 1024; // ~64 KiB
  static const char* k_log_dir   = "/log";
  static const char* k_log_path  = "/log/power.log";
  static const char* k_log_prev  = "/log/power.prev.log";
  static const char* k_logs_dir  = "/logs";
  static const char* k_resume_path = "/logs/resume.last";

  static String   s_cfg_persist_path; // optionaler Sammel-Logpfad (log.persist.path)
  static uint32_t s_cfg_persist_tail = 16384;

  void ensure_log_dirs() {
    LittleFS.mkdir(k_log_dir);
    LittleFS.mkdir(k_logs_dir);
  }
  void rotate_if_needed() {
    if (!s_log) return;
    if (s_log.size() < s_log_limit_bytes) return;
    s_log.close();
    LittleFS.remove(k_log_prev);
    LittleFS.rename(k_log_path, k_log_prev);
    s_log = LittleFS.open(k_log_path, "a");
  }
  void log_line(const String& line) {
    if (!s_log) {
      ensure_log_dirs();
      s_log = LittleFS.open(k_log_path, "a");
    }
    if (s_log) { s_log.println(line); s_log.flush(); rotate_if_needed(); }
    ::bus::emit_sticky("trace.svc.power.log", line);
  }
  String kv_get(const String& kv, const String& key) {
    int p = kv.indexOf(key + "=");
    if (p < 0) return "";
    p += key.length() + 1;
    int e = kv.indexOf(' ', p);
    if (e < 0) e = kv.length();
    return kv.substring(p, e);
  }
  void write_resume_capsule(const String& line) {
    ensure_log_dirs();
    if (File f = LittleFS.open(k_resume_path, "w")) { f.println(line); f.close(); }
    if (s_cfg_persist_path.length()) {
      if (File f2 = LittleFS.open(s_cfg_persist_path.c_str(), "a")) { f2.println(line); f2.close(); }
    }
  }
  void emit_last_resume_capsule_on_boot() {
    if (File f = LittleFS.open(k_resume_path, "r")) {
      String all = f.readString(); f.close(); all.trim();
      if (all.length()) {
        ::bus::emit_sticky("trace.svc.power.resume.persist", all);
        log_line(String("[PERSIST] ") + all);
      }
    }
  }
}

// -------------------- PMU-Objekt (keine HW-Argumente im Service!) -----------
namespace {
  static Axp2101 s_pmu; // kennt T-Watch S3 Defaults intern
}

// -------------------- Policy / State ----------------------------------------
namespace {
  static uint32_t s_autowake_ms = 0; // 0=aus

  // Guards (aus Config)
  static bool s_prevent_ls  = false;
  static bool s_prevent_sb  = false;

  // Backlight-Handling
  static int  s_ui_brightness     = -1;
  static int  s_saved_brightness  = -1;
  static bool s_dimmed_for_sleep  = false;
}

// -------------------- Telemetrie --------------------------------------------
namespace {
  void snapshot_power_telemetry(const char* phase) {
    uint16_t mv_vbat=0, mv_vsys=0, mv_vbus=0;
    bool ok1 = s_pmu.readVBAT_mV(mv_vbat);
    bool ok2 = s_pmu.readVSYS_mV(mv_vsys);
    bool ok3 = s_pmu.readVBUS_mV(mv_vbus);
    String msg = String("phase=") + phase +
                 " vbat_mv=" + String((int)mv_vbat) + (ok1?"":"?") +
                 " vsys_mv=" + String((int)mv_vsys) + (ok2?"":"?") +
                 " vbus_mv=" + String((int)mv_vbus) + (ok3?"":"?");
    ::bus::emit_sticky("state.power.telemetry", msg);
    log_line(String("[TEL] ") + msg);
  }

  void dump_irq_compact(const char* tag) {
    drv::axp2101::AxpEvents ev{};
    bool ok = s_pmu.pollIRQ(true, &ev);
    String msg = String("tag=") + tag +
                 " ok=" + (ok?"1":"0") +
                 " st1=" + String((int)ev.st1) +
                 " st2=" + String((int)ev.st2) +
                 " st3=" + String((int)ev.st3) +
                 " vbus_in=" + String(ev.vbus_in ? 1 : 0) +
                 " chg_start=" + String(ev.chg_start ? 1 : 0) +
                 " chg_done="  + String(ev.chg_done  ? 1 : 0);
    ::bus::emit_sticky("trace.svc.power.irq", msg);
    log_line(String("[IRQ] ") + msg);
  }
}

// -------------------- Backlight-Handling (Flicker-Fix) ----------------------
namespace {
  void dim_backlight_for_sleep() {
    if (s_dimmed_for_sleep) return;
    s_saved_brightness = s_ui_brightness; // kann -1 sein
    ::bus::emit_sticky("ui.brightness", "value=0 origin=power");
    log_line("[BL] dim to 0 for sleep");
    delay(60);
    s_dimmed_for_sleep = true;
  }
  void restore_backlight_after_sleep() {
    if (!s_dimmed_for_sleep) return;
    if (s_saved_brightness >= 0) {
      ::bus::emit_sticky("ui.brightness", String("value=") + String(s_saved_brightness) + " origin=power");
      log_line(String("[BL] restore=") + String(s_saved_brightness));
      delay(10);
    }
    s_dimmed_for_sleep = false;
  }
}

// -------------------- Intents ------------------------------------------------
namespace {
  void enter_ready(const String& origin) {
    ::bus::emit_sticky("power.mode_changed", String("mode=ready origin=") + origin);
    log_line(String("[MODE] ready origin=") + origin);
    restore_backlight_after_sleep();
    snapshot_power_telemetry("ready");
  }

  void enter_standby(const String& origin) {
    if (s_prevent_sb) {
      log_line(String("[BLOCK] standby (prevent_standby=1) origin=") + origin);
      ::bus::emit_sticky("trace.svc.power.block", "intent=standby reason=prevent_standby");
      return;
    }
    ::bus::emit_sticky("power.mode_changed", String("mode=standby origin=") + origin);
    log_line(String("[MODE] standby origin=") + origin);
  }

  void enter_lightsleep(const String& origin) {
    if (s_prevent_ls) {
      log_line(String("[BLOCK] lightsleep (prevent_lightsleep=1) origin=") + origin);
      ::bus::emit_sticky("trace.svc.power.block", "intent=lightsleep reason=prevent_lightsleep");
      return;
    }

    dump_irq_compact("pre_ls_dump");

    // IRQ-Latches freigeben, dann Wake armieren (HW-Details im Treiber)
    s_pmu.releaseIRQLine();
    s_pmu.armWakeGpioLow();

    // Autowake Timer
    if (s_autowake_ms > 0) {
      esp_sleep_enable_timer_wakeup((uint64_t)s_autowake_ms * 1000ULL);
      ::bus::emit_sticky("trace.svc.power.autowake", String("ms=") + String((unsigned long)s_autowake_ms));
      log_line(String("[AUTO] timer ") + String((unsigned long)s_autowake_ms) + " ms");
    }

    // Backlight ruhigstellen
    dim_backlight_for_sleep();

    ::bus::emit_sticky("power.mode_changed", String("mode=lightsleep origin=") + origin);
    log_line(String("[MODE] lightsleep origin=") + origin);

    snapshot_power_telemetry("pre_ls");

    // GO: Light-Sleep
    esp_err_t err = esp_light_sleep_start();
    const int wl = s_pmu.intLevel();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    String resume = String("[RESUME] err=") + String((int)err) +
                    " cause=" + String((int)cause) +
                    " pmu_int_lvl=" + String(wl);
    log_line(resume);
    ::bus::emit_sticky("trace.svc.power.resume", resume);

    dump_irq_compact("post_ls_dump");
    snapshot_power_telemetry("post_ls");

    // Persistente Resume-Kapsel
    {
      uint16_t mv_vbat=0, mv_vsys=0, mv_vbus=0;
      s_pmu.readVBAT_mV(mv_vbat);
      s_pmu.readVSYS_mV(mv_vsys);
      s_pmu.readVBUS_mV(mv_vbus);

      drv::axp2101::AxpEvents ev{}; s_pmu.pollIRQ(false, &ev);
      String capsule = String("RESUME t_ms=") + String(millis()) +
                       " cause=" + String((int)cause) +
                       " pmu_int_lvl=" + String(wl) +
                       " vbat_mv=" + String((int)mv_vbat) +
                       " vsys_mv=" + String((int)mv_vsys) +
                       " vbus_mv=" + String((int)mv_vbus) +
                       " irq_st=" + String((int)ev.st1) + "," + String((int)ev.st2) + "," + String((int)ev.st3);
      write_resume_capsule(capsule);
      ::bus::emit_sticky("trace.svc.power.resume.capsule", capsule);
    }

    // zurück in READY
    ::bus::emit_sticky("power.mode_changed", "mode=ready origin=lightsleep");
    log_line("[MODE] ready origin=lightsleep");
    restore_backlight_after_sleep();
  }

  void enter_deepsleep(const String& origin) {
    log_line(String("[MODE] deepsleep origin=") + origin);
    snapshot_power_telemetry("pre_ds");
    s_pmu.releaseIRQLine();
    s_pmu.armWakeGpioLow();
    esp_deep_sleep_start(); // no return
  }

  void handle_intent(const String& kv) {
    const String target = kv_get(kv, "target");
    const String origin = kv_get(kv, "origin");
    if (target == "ready")      { enter_ready(origin.length()?origin:"api");      return; }
    if (target == "standby")    { enter_standby(origin.length()?origin:"api");    return; }
    if (target == "lightsleep") { enter_lightsleep(origin.length()?origin:"api"); return; }
    if (target == "deepsleep")  { enter_deepsleep(origin.length()?origin:"api");  return; }
    ::bus::emit_sticky("trace.svc.power.warn", String("unknown_intent kv=") + kv);
    log_line(String("[WARN] unknown intent: ") + kv);
  }
}

// -------------------- Service-Init & Subscriptions ---------------------------
namespace {
  void subscribe_bus() {
    // Autowake
    ::bus::subscribe("power.sleep.autowake_ms",
      [](const String&, const String& kv){
        String v = kv_get(kv, "ms"); if (!v.length()) v = kv_get(kv, "value");
        s_autowake_ms = (uint32_t)v.toInt();
        ::bus::emit_sticky("trace.svc.power.autowake.set", String("ms=") + String((unsigned long)s_autowake_ms));
        log_line(String("[CFG] autowake_ms=") + String((unsigned long)s_autowake_ms));
      });

    // Guards
    ::bus::subscribe("power.dev.prevent_lightsleep",
      [](const String&, const String& kv){
        String v = kv_get(kv, "value"); v.toLowerCase();
        s_prevent_ls = (v=="on"||v=="1"||v=="true"||v=="yes");
        ::bus::emit_sticky("trace.svc.power.guard", String("prevent_lightsleep=") + (s_prevent_ls? "1":"0"));
      });
    ::bus::subscribe("power.dev.prevent_standby",
      [](const String&, const String& kv){
        String v = kv_get(kv, "value"); v.toLowerCase();
        s_prevent_sb = (v=="on"||v=="1"||v=="true"||v=="yes");
        ::bus::emit_sticky("trace.svc.power.guard", String("prevent_standby=") + (s_prevent_sb? "1":"0"));
      });

    // UI-Brightness (für Restore)
    ::bus::subscribe("ui.brightness",
      [](const String&, const String& kv){
        String v = kv_get(kv, "value");
        if (v.length()) s_ui_brightness = v.toInt();
      });

    // Persist-Config (optional)
    ::bus::subscribe("log.persist.path",
      [](const String&, const String& kv){
        String v = kv_get(kv, "value"); if (!v.length()) v = kv_get(kv, "path");
        s_cfg_persist_path = v;
        ::bus::emit_sticky("trace.svc.power.persist.path", String("path=") + (s_cfg_persist_path.length()? s_cfg_persist_path : "(unset)"));
      });
    ::bus::subscribe("log.persist.tail_bytes",
      [](const String&, const String& kv){
        String v = kv_get(kv, "value"); if (!v.length()) v = kv_get(kv, "bytes");
        if (v.length()) s_cfg_persist_tail = (uint32_t)v.toInt();
        ::bus::emit_sticky("trace.svc.power.persist.tail", String("bytes=") + String((unsigned long)s_cfg_persist_tail));
      });

    // On-Demand: Resume-Dump
    ::bus::subscribe("power.resume.dump",
      [](const String&, const String& /*kv*/){
        if (File f = LittleFS.open(k_resume_path, "r")) {
          String all = f.readString(); f.close(); all.trim();
          if (all.length()) {
            ::bus::emit_sticky("trace.svc.power.resume.persist", all);
            log_line(String("[DUMP] ") + all);
          } else {
            ::bus::emit_sticky("trace.svc.power.resume.persist", "EMPTY");
            log_line("[DUMP] EMPTY");
          }
        } else {
          ::bus::emit_sticky("trace.svc.power.resume.persist", "NOFILE");
          log_line("[DUMP] NOFILE");
        }
      });

    // Admin: AXP2101 IRQ-Controls (ersetzt das frühere *_irq_ext.cpp)
    ::bus::subscribe("power.axp.irq",
      [](const String& /*topic*/, const String& kv){
        const bool op_dump      = kv.indexOf("op=dump")       >= 0;
        const bool op_clear_all = kv.indexOf("op=clear_all")  >= 0;
        const bool op_en_all    = kv.indexOf("op=enable_all") >= 0;

        if (op_dump) {
          uint8_t e1=0,e2=0,e3=0, s1=0,s2=0,s3=0;
          s_pmu.getIRQEnableMask(e1,e2,e3);
          s_pmu.getIRQStatus(s1,s2,s3); // nicht-destruktiv
          ::bus::emit_sticky("trace.svc.power.irq",
             String("dump en40=0x")+String(e1,16)+" en41=0x"+String(e2,16)+" en42=0x"+String(e3,16)+
             " st48=0x"+String(s1,16)+" st49=0x"+String(s2,16)+" st4A=0x"+String(s3,16)+
             " int_lvl="+String(s_pmu.intLevel()));
          return;
        }
        if (op_clear_all) {
          uint8_t s1=0,s2=0,s3=0; s_pmu.getIRQStatus(s1,s2,s3);
          ::bus::emit_sticky("trace.svc.power.irq", String("before_clear st=") +
             String(s1,16)+","+String(s2,16)+","+String(s3,16));
          s_pmu.clearIRQStatus(); s_pmu.releaseIRQLine();
          s_pmu.getIRQStatus(s1,s2,s3);
          ::bus::emit_sticky("trace.svc.power.irq", String("after_clear st=") +
             String(s1,16)+","+String(s2,16)+","+String(s3,16) +
             " int_lvl="+String(s_pmu.intLevel()));
          return;
        }
        if (op_en_all) {
          const bool on = (kv.indexOf("value=on")>=0 || kv.indexOf("value=1")>=0 || kv.indexOf("value=true")>=0);
          s_pmu.setIRQEnableMask(on?0xFF:0x00, on?0xFF:0x00, on?0xFF:0x00);
          uint8_t e1=0,e2=0,e3=0; s_pmu.getIRQEnableMask(e1,e2,e3);
          ::bus::emit_sticky("trace.svc.power.irq", String("en_all on=")+(on?"1":"0")+
             " en40=0x"+String(e1,16)+" en41=0x"+String(e2,16)+" en42=0x"+String(e3,16));
          return;
        }
      });
  }

  void pmu_basic_setup() {
    bool ok = s_pmu.begin(400000 /*Hz*/, true /*release_irq_if_low*/);
    ::bus::emit_sticky("trace.svc.power.pmu.begin", String("ok=") + (ok?"1":"0"));
    log_line(String("[PMU] begin ok=") + (ok?"1":"0"));

    bool on = s_pmu.twatchS3_basicPowerOn();
    ::bus::emit_sticky("trace.svc.power.pmu.twatchS3", String("ok=") + (on?"1":"0"));
    log_line(String("[PMU] twatchS3_basicPowerOn ok=") + (on?"1":"0"));

    // ADC: VBAT/VSYS/VBUS aktivieren
    s_pmu.setAdcEnable(
      Axp2101::AdcCh::ADC_VBAT |
      Axp2101::AdcCh::ADC_VSYS |
      Axp2101::AdcCh::ADC_VBUS,
      true
    );

    // IRQ-Monitor an
    s_pmu.enableIRQMonitor(true);

    snapshot_power_telemetry("boot");
    dump_irq_compact("boot_irq");
  }
}

namespace svc { namespace power {

void init() {
  // LittleFS ist bereits global gemountet (siehe Boot-Logs)
  ensure_log_dirs();
  if (!s_log) s_log = LittleFS.open(k_log_path, "a");
  log_line("[BOOT] svc.power.init");

  emit_last_resume_capsule_on_boot(); // Boot-Replay

  pmu_basic_setup();
  subscribe_bus();

  ::bus::emit_sticky("trace.svc.power.policy",
                     String("pmu_int=? auto_ms=") + String((unsigned long)s_autowake_ms));

  ::bus::emit_sticky("power.mode_changed", "mode=ready origin=boot");
  log_line("[MODE] ready origin=boot");
}

} } // namespace svc::power
