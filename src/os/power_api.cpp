#include <Arduino.h>
#include <LittleFS.h>

#include "os/system_config.hpp"
#include "os/api_bus.hpp"
#include "drivers/pmu_axp2101.hpp"
#include "os/power_service.hpp"   // <-- NEU: volle Definition erforderlich
#include "os/power_api.hpp"

// (Rest der Datei unverändert)



extern SystemConfig g_cfg;
extern DevConfig    g_dev;
extern PMU_AXP2101  PMU;

static const char* modeName(PowerService::Mode m) {
  return PowerService::modeName(m);
}
static const char* profileName(PowerService::Profile p) {
  return PowerService::profileName(p);
}

void bindPowerApi(PowerService& svc, ApiBus& api) {
  // Service darf Events publizieren
  svc.attachApi(api);

  // ---------- power.* ----------
  api.registerHandler("power", [&svc, &api](const ApiRequest& r) {
    const String& act = r.action;

    if (act == "mode") {
      if (const String* v = ApiBus::findParam(r.params, "mode")) {
        String m = *v; m.toLowerCase();
        if      (m=="ready")      svc.requestMode(PowerService::Mode::Ready);
        else if (m=="standby")    svc.requestMode(PowerService::Mode::Standby);
        else if (m=="lightsleep") svc.requestMode(PowerService::Mode::LightSleep);
        else { api.replyErr(r.origin, "E_BAD_ARG","mode"); return; }
        api.replyOk(r.origin, {{"mode", m}});
      } else {
        api.replyOk(r.origin, {{"mode", modeName(svc.mode())}});
      }
      return;
    }

    if (act == "status") {
      api.replyOk(r.origin, {
        {"mode",      modeName(svc.mode())},
        {"profile",   profileName(svc.getProfile())},
        {"bl_now",    String(svc.getBacklightDutyNow())},
        {"bl_ready",  String(svc.getReadyBrightnessDuty())}
      });
      return;
    }

    if (act == "timeouts") {
      const String* a = ApiBus::findParam(r.params, "ready_s");
      const String* b = ApiBus::findParam(r.params, "standby_to_lightsleep_s");
      if (!a && !b) {
        api.replyOk(r.origin, {
          {"ready_s", String(svc.getReadyTimeoutS())},
          {"standby_to_lightsleep_s", String(svc.getStandbyToLSTimeoutS())}
        });
        return;
      }
      uint16_t rs = a ? (uint16_t)a->toInt() : svc.getReadyTimeoutS();
      uint16_t ls = b ? (uint16_t)b->toInt() : svc.getStandbyToLSTimeoutS();
      svc.setTimeouts(rs, ls);
      api.replyOk(r.origin, {{"ready_s", String(rs)}, {"standby_to_lightsleep_s", String(ls)}});
      return;
    }

    if (act == "profile") {
      if (const String* v = ApiBus::findParam(r.params, "name")) {
        String s = *v; s.toLowerCase();
        if      (s=="performance") svc.applyProfile(PowerService::Profile::Performance);
        else if (s=="endurance")   svc.applyProfile(PowerService::Profile::Endurance);
        else                       svc.applyProfile(PowerService::Profile::Balanced);
        api.replyOk(r.origin, {{"name", s}});
      } else {
        api.replyOk(r.origin, {{"name", profileName(svc.getProfile())}});
      }
      return;
    }

    if (act == "lease") {
      const String* op = ApiBus::findParam(r.params, "op");
      if (!op) { api.replyErr(r.origin, "E_BAD_ARG","op"); return; }

      if (op->equalsIgnoreCase("add")) {
        const String* t = ApiBus::findParam(r.params, "type");
        const String* ttl = ApiBus::findParam(r.params, "ttl_ms");
        if (!t || !ttl) { api.replyErr(r.origin, "E_BAD_ARG","type|ttl_ms"); return; }
        String tl = *t; tl.toLowerCase();
        PowerService::LeaseType lt;
        if      (tl=="keep_awake") lt = PowerService::LeaseType::KEEP_AWAKE;
        else if (tl=="bl_pulse")   lt = PowerService::LeaseType::BL_PULSE;
        else if (tl=="lora_rx")    lt = PowerService::LeaseType::LORA_RX;
        else { api.replyErr(r.origin, "E_BAD_ARG","type"); return; }
        uint16_t id = svc.addLease(lt, (uint32_t)ttl->toInt());
        if (!id) { api.replyErr(r.origin, "E_FAIL","no_slot"); return; }
        api.replyOk(r.origin, {{"id", String(id)}});
        return;
      }

      if (op->equalsIgnoreCase("drop")) {
        const String* id = ApiBus::findParam(r.params, "id");
        if (!id) { api.replyErr(r.origin, "E_BAD_ARG","id"); return; }
        svc.dropLease((uint16_t)id->toInt());
        api.replyOk(r.origin, {});
        return;
      }

      api.replyErr(r.origin, "E_BAD_ARG","op");
      return;
    }

    // power.set avoid_ls_when_usb=on|off
    if (act == "set") {
      const String* v = ApiBus::findParam(r.params, "avoid_ls_when_usb");
      if (!v) { api.replyErr(r.origin, "E_BAD_ARG","avoid_ls_when_usb"); return; }
      bool en = v->equalsIgnoreCase("on") || *v=="1";
      svc.setAvoidLightSleepWhenUSB(en);
      // **optional**: auch in Dev-Config merken (persist via config.save)
      g_dev.debug_avoid_ls_when_usb = en;
      api.replyOk(r.origin, {{"avoid_ls_when_usb", en ? "on" : "off"}});
      return;
    }

    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- wake.* ----------
  api.registerHandler("wake", [&svc, &api](const ApiRequest& r) {
    if (r.action == "get") {
      auto const& w = svc.getWakePolicy();
      const char* btn = (w.button_short==PowerService::WakePolicy::ButtonShort::ToggleReadyStandby) ? "toggle_ready_standby" : "none";
      api.replyOk(r.origin, {
        {"touch",       w.touch ? "on":"off"},
        {"radio_event", w.radio_event ? "on":"off"},
        {"motion",      w.motion ? "on":"off"},
        {"button_short", btn}
      });
      return;
    }
    if (r.action == "set") {
      auto w = svc.getWakePolicy();
      if (auto v = ApiBus::findParam(r.params,"touch"))       w.touch = v->equalsIgnoreCase("on") || *v=="1";
      if (auto v = ApiBus::findParam(r.params,"radio_event")) w.radio_event = v->equalsIgnoreCase("on") || *v=="1";
      if (auto v = ApiBus::findParam(r.params,"motion"))      w.motion = v->equalsIgnoreCase("on") || *v=="1";
      if (auto v = ApiBus::findParam(r.params,"button_short")) {
        if (v->equalsIgnoreCase("toggle_ready_standby")) w.button_short = PowerService::WakePolicy::ButtonShort::ToggleReadyStandby;
        else if (v->equalsIgnoreCase("none"))            w.button_short = PowerService::WakePolicy::ButtonShort::None;
        else { api.replyErr(r.origin, "E_BAD_ARG","button_short"); return; }
      }
      svc.setWakePolicy(w);
      api.replyOk(r.origin, {{"ok","1"}});
      return;
    }
    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- quiet.* ----------
  api.registerHandler("quiet", [&svc, &api](const ApiRequest& r) {
    if (r.action == "get") {
      auto const& q = svc.getQuiet();
      api.replyOk(r.origin, {
        {"enable", q.enable? "on":"off"},
        {"start_min", String(q.start_min)},
        {"end_min",   String(q.end_min)},
        {"screen_on_on_event", q.screen_on_on_event? "on":"off"},
        {"haptics",  q.haptics? "on":"off"},
        {"bl_cap_pct", String(svc.getQuietCapPct())}
      });
      return;
    }
    if (r.action == "set") {
      auto q = svc.getQuiet();
      bool touched = false;
      if (auto v = ApiBus::findParam(r.params,"enable")) { q.enable = v->equalsIgnoreCase("on") || *v=="1"; touched=true; }
      if (auto v = ApiBus::findParam(r.params,"start_min")) { q.start_min = (uint16_t)v->toInt(); touched=true; }
      if (auto v = ApiBus::findParam(r.params,"end_min"))   { q.end_min   = (uint16_t)v->toInt(); touched=true; }
      if (auto v = ApiBus::findParam(r.params,"screen_on_on_event")) { q.screen_on_on_event = v->equalsIgnoreCase("on") || *v=="1"; touched=true; }
      if (auto v = ApiBus::findParam(r.params,"haptics")) { q.haptics = v->equalsIgnoreCase("on") || *v=="1"; touched=true; }
      if (auto v = ApiBus::findParam(r.params,"bl_cap_pct")) {
        uint8_t cap = (uint8_t)constrain(v->toInt(), 10, 100);
        svc.setQuietCapPct(cap);
        g_cfg.quiet_bl_cap_pct = cap; // für persist
      }
      if (touched) svc.setQuiet(q);
      // falls Ready → BL sofort nachziehen
      svc.setReadyBrightness(svc.getReadyBrightnessDuty());
      api.replyOk(r.origin, {{"ok","1"}});
      return;
    }
    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- clock.* ----------
  api.registerHandler("clock", [&svc, &api](const ApiRequest& r) {
    if (r.action == "get") {
      api.replyOk(r.origin, {{"now_min", String(svc.getNowMin())}});
      return;
    }
    if (r.action == "set") {
      if (auto v = ApiBus::findParam(r.params,"now_min")) {
        svc.setNowMin((uint16_t)constrain(v->toInt(),0,1439));
        api.replyOk(r.origin, {{"now_min", String(svc.getNowMin())}});
        return;
      }
      api.replyErr(r.origin, "E_BAD_ARG","now_min");
      return;
    }
    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- radio.* ----------
  api.registerHandler("radio", [&svc, &api](const ApiRequest& r) {
    auto m3name = [](PowerService::RadioPolicy::Mode3 m)->const char*{
      return (m==PowerService::RadioPolicy::Mode3::Off)?"off":(m==PowerService::RadioPolicy::Mode3::On)?"on":"auto";
    };
    auto lrxname = [](PowerService::RadioPolicy::LoRaRx m)->const char*{
      return (m==PowerService::RadioPolicy::LoRaRx::Off)?"off":(m==PowerService::RadioPolicy::LoRaRx::Periodic)?"periodic":"always";
    };

    if (r.action == "get") {
      auto const& rp = svc.getRadioPolicy();
      api.replyOk(r.origin, {
        {"ble",  m3name(rp.ble)},
        {"wifi", m3name(rp.wifi)},
        {"lora", lrxname(rp.lora)},
        {"lora_period_s", String(rp.lora_period_s)}
      });
      return;
    }
    if (r.action == "set") {
      auto rp = svc.getRadioPolicy();
      if (auto v = ApiBus::findParam(r.params,"ble")) {
        if (v->equalsIgnoreCase("off")) rp.ble = PowerService::RadioPolicy::Mode3::Off;
        else if (v->equalsIgnoreCase("on")) rp.ble = PowerService::RadioPolicy::Mode3::On;
        else if (v->equalsIgnoreCase("auto")) rp.ble = PowerService::RadioPolicy::Mode3::Auto;
        else { api.replyErr(r.origin, "E_BAD_ARG","ble"); return; }
      }
      if (auto v = ApiBus::findParam(r.params,"wifi")) {
        if (v->equalsIgnoreCase("off")) rp.wifi = PowerService::RadioPolicy::Mode3::Off;
        else if (v->equalsIgnoreCase("on")) rp.wifi = PowerService::RadioPolicy::Mode3::On;
        else if (v->equalsIgnoreCase("auto")) rp.wifi = PowerService::RadioPolicy::Mode3::Auto;
        else { api.replyErr(r.origin, "E_BAD_ARG","wifi"); return; }
      }
      if (auto v = ApiBus::findParam(r.params,"lora")) {
        String s=*v; s.toLowerCase();
        if      (s=="off")      rp.lora = PowerService::RadioPolicy::LoRaRx::Off;
        else if (s=="periodic") rp.lora = PowerService::RadioPolicy::LoRaRx::Periodic;
        else if (s=="always")   rp.lora = PowerService::RadioPolicy::LoRaRx::Always;
        else { api.replyErr(r.origin, "E_BAD_ARG","lora"); return; }
      }
      if (auto v = ApiBus::findParam(r.params,"lora_period_s")) rp.lora_period_s = (uint16_t)v->toInt();
      svc.setRadioPolicy(rp);
      api.replyOk(r.origin, {{"ok","1"}});
      return;
    }
    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- display.* ----------
  api.registerHandler("display", [&svc, &api](const ApiRequest& r) {
    if (r.action == "brightness") {
      const String* p = ApiBus::findParam(r.params,"pct");
      if (!p) { api.replyErr(r.origin, "E_BAD_ARG","pct"); return; }
      int pct = constrain(p->toInt(), 0, 100);
      uint8_t duty = (uint8_t)map(pct, 0, 100, 0, 255);
      svc.setReadyBrightness(duty);
      api.replyOk(r.origin, {{"pct", String(pct)}, {"duty", String(duty)}});
      return;
    }
    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });

  // ---------- config.* ----------
  api.registerHandler("config", [&api, &svc](const ApiRequest& r) {
    if (r.action == "get") {
      const String* scope = ApiBus::findParam(r.params, "scope");
      if (scope && scope->equalsIgnoreCase("dev")) {
        // Dev-Snapshot
        api.replyOk(r.origin, {
          {"debug.avoid_ls_when_usb", g_dev.debug_avoid_ls_when_usb ? "on":"off"},
          {"pmu_limits.charge_target_mV_min", String(g_dev.pmu_limits.charge_target_mV_min)},
          {"pmu_limits.charge_target_mV_max", String(g_dev.pmu_limits.charge_target_mV_max)},
          {"pmu_limits.vbus_limit_mA_min",    String(g_dev.pmu_limits.vbus_limit_mA_min)},
          {"pmu_limits.vbus_limit_mA_max",    String(g_dev.pmu_limits.vbus_limit_mA_max)},
          {"rails.backlight_mV", String(g_dev.rails.backlight_mV)},
          {"rails.lora_vdd_mV",  String(g_dev.rails.lora_vdd_mV)},
          {"rails.lora_pa_mV",   String(g_dev.rails.lora_pa_mV)},
          {"rails.vibra_mV",     String(g_dev.rails.vibra_mV)}
        });
        return;
      }
      // System-Snapshot (user-facing)
      api.replyOk(r.origin, {
        {"profile", g_cfg.power_profile},
        {"display.brightness_min", String(g_cfg.display.brightness_min)},
        {"display.brightness_max", String(g_cfg.display.brightness_max)},
        {"display.timeout_ready_s", String(g_cfg.display.timeout_ready_s)},
        {"display.timeout_standby_to_lightsleep_s", String(g_cfg.display.timeout_standby_to_lightsleep_s)},
        {"wakeup.button_short", g_cfg.wake_button_short},
        {"wakeup.touch",  g_cfg.wake_touch ? "on":"off"},
        {"wakeup.motion", g_cfg.wake_motion ? "on":"off"},
        {"wakeup.radio_event", g_cfg.wake_radio_event ? "on":"off"},
        {"quiet.enable", g_cfg.quiet_enable ? "on":"off"},
        {"quiet.start_min", String(g_cfg.quiet_start_min)},
        {"quiet.end_min",   String(g_cfg.quiet_end_min)},
        {"quiet.screen_on_on_event", g_cfg.quiet_screen_on_on_event ? "on":"off"},
        {"quiet.haptics",            g_cfg.quiet_haptics ? "on":"off"},
        {"quiet.bl_cap_pct",         String(g_cfg.quiet_bl_cap_pct)},
        {"radio.ble",  g_cfg.radio_ble},
        {"radio.wifi", g_cfg.radio_wifi},
        {"radio.lora_rx_policy", g_cfg.lora_rx_policy},
        {"radio.lora_period_s", String(g_cfg.lora_period_s)},
        {"charger.mode", g_cfg.charger_mode},
        {"pmu.charge_target_mV", String(g_cfg.pmu.charge_target_mV)},
        {"pmu.vbus_limit_mA",    String(g_cfg.pmu.vbus_limit_mA)}
      });
      return;
    }

    if (r.action == "set") {
      // aktuell nur: quiet.bl_cap_pct (user-facing)
      if (auto v = ApiBus::findParam(r.params,"quiet.bl_cap_pct")) {
        uint8_t cap = (uint8_t)constrain(v->toInt(), 10, 100);
        g_cfg.quiet_bl_cap_pct = cap;
        svc.setQuietCapPct(cap);
        // falls Ready → BL sofort nachziehen
        svc.setReadyBrightness(svc.getReadyBrightnessDuty());
        api.replyOk(r.origin, {{"quiet.bl_cap_pct", String(cap)}});
        return;
      }
      api.replyErr(r.origin, "E_BAD_ARG","supported: quiet.bl_cap_pct");
      return;
    }

    if (r.action == "save") {
      bool ok1 = g_cfg.save(LittleFS, USER_CFG_PATH);
      bool ok2 = g_dev.save(LittleFS, DEV_CFG_PATH);
      api.replyOk(r.origin, {
        {"system_saved", ok1?"1":"0"},
        {"dev_saved",    ok2?"1":"0"}
      });
      return;
    }

    if (r.action == "load") {
      SystemConfig tmp;
      DevConfig    dtmp;
      tmp.load(LittleFS, USER_CFG_PATH);
      dtmp.load(LittleFS, DEV_CFG_PATH);
      clampWithDev(tmp, dtmp);
      g_cfg = tmp;
      g_dev = dtmp;

      // Re-apply PMU + PowerService live
      PMU.setChargeTargetMillivolts(g_cfg.pmu.charge_target_mV);
      PMU.setVbusLimitMilliamp(g_cfg.pmu.vbus_limit_mA);

      svc.begin(g_cfg, &PMU);
      svc.setAvoidLightSleepWhenUSB(g_dev.debug_avoid_ls_when_usb);
      svc.setQuietCapPct(g_cfg.quiet_bl_cap_pct);

      api.replyOk(r.origin, {{"loaded","1"}});
      return;
    }

    api.replyErr(r.origin, "E_NO_ACT","unknown action");
  });
}
