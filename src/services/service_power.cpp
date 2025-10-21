// Power-Service (gehärtet):
//  - Backlight via ui.brightness (0/restore) + power.mode_changed
//  - Light-Sleep mit GPIO- und/oder Timer-Wake (RTC-Periph on)
//  - AXP2101 PMU-INT dump+clear wenn low
//  - Explizites (Re-)Konfigurieren der Wake-Sources pro Sleep-Zyklus

#include "service_power.hpp"
#include "../drivers/drv_power_axp2101.hpp"
#include <Wire.h>

#include <Arduino.h>
#include "../core/bus.hpp"

#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_err.h"

namespace svc { namespace power {

static inline void CONSOLE_MUTE(bool on) {
  bus::emit_sticky("console.mute", String("value=") + (on ? "on" : "off"));
}

// --- Pins (T-Watch S3) -------------------------------------------------------
static constexpr gpio_num_t PIN_PMU_INT   = GPIO_NUM_21; // AXP2101 INT (LOW)
static constexpr gpio_num_t PIN_RTC_INT   = GPIO_NUM_17; // PCF8563 INT (LOW)
static constexpr gpio_num_t PIN_LORA_DIO1 = GPIO_NUM_9;  // SX1262 DIO1 (HIGH)
static constexpr gpio_num_t PIN_TOUCH_INT = GPIO_NUM_16; // FT6336U INT (LOW)

static drv::axp2101::Axp2101 s_pmu(&Wire, 0x34, (int)PIN_PMU_INT, 10, 11);
static uint8_t s_last_brightness_pct = 65;

// --- Wake-Policy -------------------------------------------------------------
static volatile bool s_wake_user_btn      = true;
static volatile bool s_wake_rtc           = true;
static volatile bool s_wake_lora_dio1     = true;
static volatile bool s_wake_touch_ls      = false;
static volatile uint32_t s_autowake_ms    = 0;

// --- Trace helpers -----------------------------------------------------------
static inline void TRACE(const char* topic, const String& msg) { bus::emit_sticky(String(topic), msg); }
static inline void TRACE_WAKE_KV(const char* key, int v) { bus::emit_sticky("trace.svc.power.wake", String(key) + "=" + String(v)); }
static inline void EMIT(const char* topic, const String& kv) { bus::emit_sticky(String(topic), kv); }

// --- KV helpers --------------------------------------------------------------
static inline String kv_value(const String& v) { int p=v.indexOf('='); return (p>=0) ? v.substring(p+1) : v; }
static inline bool parse_onoff(const String& v) { String x=kv_value(v); x.toLowerCase(); return (x=="on"||x=="1"||x=="true"); }
static inline uint32_t parse_u32ms(const String& v) { return (uint32_t) kv_value(v).toInt(); }

// --- GPIO helpers ------------------------------------------------------------
static void cfg_pull(gpio_num_t pin, bool pullup, bool pulldown) {
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  if (pullup)      gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  else if (pulldown) gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
  else             gpio_set_pull_mode(pin, GPIO_FLOATING);
}
static inline int read_level(gpio_num_t pin) { return gpio_get_level(pin); }

static void wake_enable_pin(gpio_num_t pin, bool level_high_active) {
  gpio_wakeup_enable(pin, level_high_active ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
}
static void wake_disable_pin(gpio_num_t pin) { gpio_wakeup_disable(pin); }

// --- Sleep plan --------------------------------------------------------------
struct WakePlan {
  bool use_user_btn;
  bool use_rtc;
  bool use_lora;
  bool use_touch;
  int  pmu_lvl, rtc_lvl, lora_lvl, touch_lvl;
};

static void disable_all_wake_sources() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  wake_disable_pin(PIN_PMU_INT);
  wake_disable_pin(PIN_RTC_INT);
  wake_disable_pin(PIN_LORA_DIO1);
  wake_disable_pin(PIN_TOUCH_INT);
}

static bool enable_timer_if_configured() {
  uint32_t ms = s_autowake_ms;
  if (ms == 0) return false;

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);

  (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_err_t e2 = esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  TRACE("trace.svc.power.sleep", String("autowake_ms=") + String(ms) + " err=" + String((int)e2));
  return e2 == ESP_OK;
}

static bool make_wake_plan(WakePlan& plan) {
  plan.use_user_btn = s_wake_user_btn;
  plan.use_rtc      = s_wake_rtc;
  plan.use_lora     = s_wake_lora_dio1;
  plan.use_touch    = s_wake_touch_ls;

  if (plan.use_user_btn) { cfg_pull(PIN_PMU_INT,   true,  false); }
  if (plan.use_rtc)      { cfg_pull(PIN_RTC_INT,   true,  false); }
  if (plan.use_lora)     { cfg_pull(PIN_LORA_DIO1, false, true ); }
  if (plan.use_touch)    { cfg_pull(PIN_TOUCH_INT, true,  false); }

  plan.pmu_lvl   = read_level(PIN_PMU_INT);
  plan.rtc_lvl   = read_level(PIN_RTC_INT);
  plan.lora_lvl  = read_level(PIN_LORA_DIO1);
  plan.touch_lvl = read_level(PIN_TOUCH_INT);

  TRACE("trace.svc.power.sleep.lines",
        String("pmu=")   + plan.pmu_lvl   +
        " rtc="          + plan.rtc_lvl   +
        " lora="         + plan.lora_lvl  +
        " touch="        + plan.touch_lvl);

  if (plan.use_user_btn && plan.pmu_lvl == 0) {
    TRACE("trace.svc.power.sleep", "pmu_irq=latched -> dump+clear");
    s_pmu.dumpIRQ(); delay(2);
    s_pmu.clearIRQStatus(); s_pmu.releaseIRQLine(); delay(2);
    plan.pmu_lvl = read_level(PIN_PMU_INT);
    TRACE("trace.svc.power.sleep", String("pmu_irq after_clear lvl=") + String(plan.pmu_lvl));
  }

  bool any_gpio_enabled = false;
  if (plan.use_user_btn) { wake_enable_pin(PIN_PMU_INT,   false); any_gpio_enabled = true; }
  if (plan.use_rtc)      { wake_enable_pin(PIN_RTC_INT,   false); any_gpio_enabled = true; }
  if (plan.use_lora)     { wake_enable_pin(PIN_LORA_DIO1, true ); any_gpio_enabled = true; }
  if (plan.use_touch)    { wake_enable_pin(PIN_TOUCH_INT, false); any_gpio_enabled = true; }
  if (any_gpio_enabled)  { esp_sleep_enable_gpio_wakeup(); }

  if (s_autowake_ms == 0) {
    bool has_inactive_ready = false;
    if (plan.use_user_btn && (plan.pmu_lvl   != 0)) has_inactive_ready = true;
    if (plan.use_rtc      && (plan.rtc_lvl   != 0)) has_inactive_ready = true;
    if (plan.use_lora     && (plan.lora_lvl  == 0)) has_inactive_ready = true;
    if (plan.use_touch    && (plan.touch_lvl != 0)) has_inactive_ready = true;
    if (!has_inactive_ready) {
      TRACE("trace.svc.power.sleep", "skip=lightsleep reason=active_wake_line_present");
      return false;
    }
  }
  return true;
}

static void cleanup_wake_pins() {
  wake_disable_pin(PIN_PMU_INT);
  wake_disable_pin(PIN_RTC_INT);
  wake_disable_pin(PIN_LORA_DIO1);
  wake_disable_pin(PIN_TOUCH_INT);
}

// --- Wake cause log ----------------------------------------------------------
static void log_wake_cause_after_resume(uint32_t slept_ms, const WakePlan& plan_before) {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  String msg = String("resume cause=");
  switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:      msg += "gpio"; break;
    case ESP_SLEEP_WAKEUP_TIMER:     msg += "timer"; break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: msg += "undefined"; break;
    default:                         msg += "other"; break;
  }
  msg += " slept_ms=" + String(slept_ms);

  int pmu   = read_level(PIN_PMU_INT);
  int rtc   = read_level(PIN_RTC_INT);
  int lora  = read_level(PIN_LORA_DIO1);
  int touch = read_level(PIN_TOUCH_INT);

  String by = "unknown";
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    if (plan_before.use_user_btn && pmu == 0)        by = "pmu_key";
    else if (plan_before.use_rtc && rtc == 0)        by = "rtc";
    else if (plan_before.use_lora && lora == 1)      by = "lora_dio1";
    else if (plan_before.use_touch && touch == 0)    by = "touch";
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    by = "timer";
  }
  msg += " by=" + by;
  TRACE("trace.svc.power.sleep", msg);
}

// --- UI brightness tracking --------------------------------------------------
static void on_ui_brightness(const String& /*topic*/, const String& v) {
  String s = v; int eq = s.indexOf('=');
  if (eq >= 0) s = s.substring(eq + 1);
  int pct = s.toInt();
  if (pct >= 0 && pct <= 100) s_last_brightness_pct = (uint8_t)pct;
}

// --- Intent handling ---------------------------------------------------------
static void handle_intent(const String& kv) {
  int i = kv.indexOf("target=");
  String target;
  if (i >= 0) {
    int s = i + 7;
    int e = kv.indexOf(' ', s);
    if (e < 0) e = kv.length();
    target = kv.substring(s, e);
  }

  if (target == "standby") {
    EMIT("ui.brightness", "value=0");
    EMIT("power.mode_changed", "mode=standby");
    TRACE("trace.svc.power.apply", String("key=power.intent value=") + kv);
    return;
  }

  if (target == "ready") {
    EMIT("ui.brightness", String("value=") + String(s_last_brightness_pct));
    EMIT("power.mode_changed", "mode=ready");
    TRACE("trace.svc.power.apply", String("key=power.intent value=") + kv);
    return;
  }

  if (target == "lightsleep") {
    if (!s_wake_user_btn && !s_wake_rtc && !s_wake_lora_dio1 && !s_wake_touch_ls && s_autowake_ms==0) {
      TRACE("trace.svc.power.sleep", "skip=lightsleep reason=no_wake_source_enabled");
      return;
    }

    // --- Konsole muten (keine USB/Writes während kritischer Phase) ---------
    CONSOLE_MUTE(true);

    // Backlight aus + LS-Mode announcen
    EMIT("ui.brightness", "value=0");
    EMIT("power.mode_changed", "mode=lightsleep");

    // Wake-Sources sauber neu konfigurieren
    disable_all_wake_sources();
    (void)enable_timer_if_configured();

    WakePlan plan{};
    if (!make_wake_plan(plan)) {
      cleanup_wake_pins();
      EMIT("power.mode_changed", "mode=ready");
      EMIT("ui.brightness", String("value=") + String(s_last_brightness_pct));
      // kleine Gnadenfrist, dann Konsole wieder frei
      delay(50);
      CONSOLE_MUTE(false);
      return;
    }

    // Last-Call
    TRACE("trace.svc.power.sleep", "enter last_call=begin");
    bus::emit_sticky("sys.lastcall", "op=begin");
    bus::emit_sticky("sys.lastcall", "op=end");
    TRACE("trace.svc.power.sleep", "enter last_call=end");

    if (Serial) { Serial.flush(); }
    delay(5);

    // Light-Sleep
    uint32_t t0 = millis();
    esp_light_sleep_start();
    uint32_t slept = millis() - t0;

    // Resume
    log_wake_cause_after_resume(slept, plan);
    cleanup_wake_pins();

    // Zurück zu ready + Helligkeit wiederherstellen
    EMIT("power.mode_changed", "mode=ready");
    EMIT("ui.brightness", String("value=") + String(s_last_brightness_pct));

    // Kurzer Settling-Delay, dann Konsole wieder freigeben
    delay(150);
    CONSOLE_MUTE(false);
    return;
  }

  // andere Ziele → nur Telemetrie
  TRACE("trace.svc.power.apply", String("key=power.intent value=") + kv);
}

// --- Policy events -----------------------------------------------------------
static void on_wake_evt(const String& topic, const String& value) {
  if (topic == "wake.user_btn")            { s_wake_user_btn  = parse_onoff(value); TRACE_WAKE_KV("user_btn",        s_wake_user_btn);  return; }
  if (topic == "wake.rtc")                 { s_wake_rtc       = parse_onoff(value); TRACE_WAKE_KV("rtc",             s_wake_rtc);       return; }
  if (topic == "wake.lora_dio1")           { s_wake_lora_dio1 = parse_onoff(value); TRACE_WAKE_KV("lora_dio1",       s_wake_lora_dio1); return; }
  if (topic == "wake.touch_lightsleep")    { s_wake_touch_ls  = parse_onoff(value); TRACE_WAKE_KV("touch_lightsleep",s_wake_touch_ls);  return; }
  if (topic == "power.sleep.autowake_ms")  {
    s_autowake_ms = parse_u32ms(value);
    TRACE("trace.svc.power.sleep", String("cfg autowake_ms=")+String(s_autowake_ms));
    return;
  }
}

// --- Init --------------------------------------------------------------------
void init() {
  bool pmu_ok = s_pmu.begin(400000, true);
  if (pmu_ok) {
    s_pmu.twatchS3_basicPowerOn();
    s_pmu.enableIRQMonitor(true);
    uint8_t dcdc=0,en0=0,en1=0;
    s_pmu.getDcdcOnOff(dcdc); s_pmu.getLdoOnOff(en0,en1);
    TRACE("power.rails", String("dcdc=0x")+String(dcdc,HEX)+
                         " ldo0=0x"+String(en0,HEX)+
                         " ldo1=0x"+String(en1,HEX));
  } else {
    TRACE("warn.svc.power", "axp.begin=fail");
  }

  bus::subscribe("wake.*", on_wake_evt);
  bus::subscribe("power.sleep.autowake_ms", on_wake_evt);
  bus::subscribe("ui.brightness", on_ui_brightness);

  bus::subscribe("power.intent",
    [](const String& /*topic*/, const String& kv){ handle_intent(kv); });

  TRACE_WAKE_KV("user_btn",        s_wake_user_btn ? 1 : 0);
  TRACE_WAKE_KV("rtc",             s_wake_rtc ? 1 : 0);
  TRACE_WAKE_KV("lora_dio1",       s_wake_lora_dio1 ? 1 : 0);
  TRACE_WAKE_KV("touch_lightsleep",s_wake_touch_ls ? 1 : 0);
}

} } // namespace svc::power
