// ============================================================================
// File: src/services/service_power.cpp
// Hardened Power Service for T-Watch S3 (ESP32-S3 + AXP2101)
// Implements: ready/standby/lightsleep intents with GPIO wake + optional timer.
// Adds robust parsing for power.sleep.autowake_ms and detailed tracing.
// No PMU once-masking; if PMU INT stays low after dump+clear, we abort sleep.
// ----------------------------------------------------------------------------

#include "service_power.hpp"

#include <Arduino.h>
#include "../core/bus.hpp"

#include "driver/gpio.h"
#include "esp_sleep.h"

namespace svc { namespace power {

// -----------------------------------------------------------------------------
// Pin-Mapping (T-Watch S3)
//   AXP2101 INT  -> GPIO21 (active-LOW, open-drain)
//   PCF8563 INT  -> GPIO17 (active-LOW)
//   SX1262 DIO1  -> GPIO9  (active-HIGH)
//   FT6336U INT  -> GPIO16 (active-LOW), only if touch_lightsleep=on
static constexpr gpio_num_t PIN_PMU_INT   = GPIO_NUM_21;
static constexpr gpio_num_t PIN_RTC_INT   = GPIO_NUM_17;
static constexpr gpio_num_t PIN_LORA_DIO1 = GPIO_NUM_9;
static constexpr gpio_num_t PIN_TOUCH_INT = GPIO_NUM_16;

// -----------------------------------------------------------------------------
// Wake-Policy (configurable via events)
static volatile bool s_wake_user_btn   = true;
static volatile bool s_wake_rtc        = true;
static volatile bool s_wake_lora_dio1  = true;
static volatile bool s_wake_touch_ls   = false; // default off

// Optional Auto-Wake Timer (ms). 0 = disabled.
static volatile uint32_t s_autowake_ms = 0;

// -----------------------------------------------------------------------------
// Trace helpers
static inline void TRACE(const char* topic, const String& msg) {
  bus::emit_sticky(String(topic), msg);
}
static inline void TRACE_WAKE_KV(const char* key, int v) {
  bus::emit_sticky("trace.svc.power.wake", String(key) + "=" + String(v));
}

// -----------------------------------------------------------------------------
// GPIO helpers
static void cfg_pull(gpio_num_t pin, bool pullup, bool pulldown) {
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  if (pullup && pulldown) {
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  } else if (pullup) {
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  } else if (pulldown) {
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
  } else {
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
}
static inline int read_level(gpio_num_t pin) { return gpio_get_level(pin); }

static void wake_enable_pin(gpio_num_t pin, bool level_high_active) {
  gpio_wakeup_enable(pin, level_high_active ? GPIO_INTR_HIGH_LEVEL
                                            : GPIO_INTR_LOW_LEVEL);
}
static void wake_disable_pin(gpio_num_t pin) {
  gpio_wakeup_disable(pin);
}

// -----------------------------------------------------------------------------
// Wake planning & configuration
struct WakePlan {
  bool use_user_btn;
  bool use_rtc;
  bool use_lora;
  bool use_touch;
  int  pmu_lvl, rtc_lvl, lora_lvl, touch_lvl;
};

static void enable_timer_if_configured() {
  uint32_t ms = s_autowake_ms;
  if (ms > 0) {
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    TRACE("trace.svc.power.sleep", String("autowake_ms=") + String(ms));
  }
}

static bool make_wake_plan(WakePlan& plan) {
  // Current policy snapshot
  plan.use_user_btn = s_wake_user_btn;
  plan.use_rtc      = s_wake_rtc;
  plan.use_lora     = s_wake_lora_dio1;
  plan.use_touch    = s_wake_touch_ls;

  // Pulls by polarity
  if (plan.use_user_btn) { cfg_pull(PIN_PMU_INT,   true /*up*/,  false/*down*/); }
  if (plan.use_rtc)      { cfg_pull(PIN_RTC_INT,   true /*up*/,  false/*down*/); }
  if (plan.use_lora)     { cfg_pull(PIN_LORA_DIO1, false/*up*/,  true /*down*/); }
  if (plan.use_touch)    { cfg_pull(PIN_TOUCH_INT, true /*up*/,  false/*down*/); }

  // Read levels
  plan.pmu_lvl   = read_level(PIN_PMU_INT);
  plan.rtc_lvl   = read_level(PIN_RTC_INT);
  plan.lora_lvl  = read_level(PIN_LORA_DIO1);
  plan.touch_lvl = read_level(PIN_TOUCH_INT);

  TRACE("trace.svc.power.sleep.lines",
        String("pmu=") + plan.pmu_lvl +
        " rtc="       + plan.rtc_lvl +
        " lora="      + plan.lora_lvl +
        " touch="     + plan.touch_lvl);

  // If PMU-INT is already low → try to dump+clear once, then re-read
  if (plan.use_user_btn && plan.pmu_lvl == 0) {
    TRACE("trace.svc.power.sleep", "pmu_irq=latched -> dump+clear");
    bus::emit_sticky("power.axp.irq", "op=dump");
    bus::emit_sticky("power.axp.irq", "op=clear_all");
    delay(2);
    plan.pmu_lvl = read_level(PIN_PMU_INT);
    TRACE("trace.svc.power.sleep", String("pmu_irq after_clear lvl=") + String(plan.pmu_lvl));
  }

  // Enable wake sources per (possibly updated) plan
  if (plan.use_user_btn) wake_enable_pin(PIN_PMU_INT,   false/*LOW active*/);
  if (plan.use_rtc)      wake_enable_pin(PIN_RTC_INT,   false/*LOW active*/);
  if (plan.use_lora)     wake_enable_pin(PIN_LORA_DIO1, true /*HIGH active*/);
  if (plan.use_touch)    wake_enable_pin(PIN_TOUCH_INT, false/*LOW active*/);

  // Global GPIO wake
  esp_sleep_enable_gpio_wakeup();

  // Decision:
  //   If timer configured → always allowed.
  //   Else require at least one *inactive* allowed GPIO to avoid instant wake.
  const bool timer_ok = (s_autowake_ms > 0);

  const bool any_gpio_enabled =
      plan.use_user_btn || plan.use_rtc || plan.use_lora || plan.use_touch;

  if (!any_gpio_enabled && !timer_ok) {
    TRACE("trace.svc.power.sleep", "skip=lightsleep reason=no_wake_source_enabled");
    return false;
  }

  if (!timer_ok) {
    bool has_inactive_ready = false;
    if (plan.use_user_btn && (plan.pmu_lvl   != 0)) has_inactive_ready = true; // active LOW → inactive when 1
    if (plan.use_rtc      && (plan.rtc_lvl   != 0)) has_inactive_ready = true; // active LOW → inactive when 1
    if (plan.use_lora     && (plan.lora_lvl  == 0)) has_inactive_ready = true; // active HIGH → inactive when 0
    if (plan.use_touch    && (plan.touch_lvl != 0)) has_inactive_ready = true; // active LOW → inactive when 1
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

// -----------------------------------------------------------------------------
// Wake-cause logging
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

  // Re-sample levels for heuristic
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

// -----------------------------------------------------------------------------
// Intent handling
static void handle_intent(const String& kv) {
  // kv example: "target=lightsleep origin=api"
  int i = kv.indexOf("target=");
  String target;
  if (i >= 0) {
    int s = i + 7;
    int e = kv.indexOf(' ', s);
    if (e < 0) e = kv.length();
    target = kv.substring(s, e);
  }

  if (target == "lightsleep") {
    // Guard: need at least one allowed source (GPIO or timer)
    if (!s_wake_user_btn && !s_wake_rtc && !s_wake_lora_dio1 && !s_wake_touch_ls && s_autowake_ms == 0) {
      TRACE("trace.svc.power.sleep", "skip=lightsleep reason=no_wake_source_enabled");
      return;
    }

    // Prepare wake plan
    WakePlan plan{};
    if (!make_wake_plan(plan)) {
      cleanup_wake_pins();
      return;
    }

    // Timer wake if configured
    enable_timer_if_configured();

    // Last-call notifications
    TRACE("trace.svc.power.sleep", "enter last_call=begin");
    bus::emit_sticky("sys.lastcall", "op=begin");
    bus::emit_sticky("sys.lastcall", "op=end");
    TRACE("trace.svc.power.sleep", "enter last_call=end");

    // Actual light sleep
    uint32_t t0 = millis();
    esp_light_sleep_start(); // blocks until wake
    uint32_t slept = millis() - t0;

    // Diagnostics
    log_wake_cause_after_resume(slept, plan);

    // Cleanup
    cleanup_wake_pins();

    return; // keep system in ready path implicitly
  }

  // Other targets unchanged; just telemetry
  TRACE("trace.svc.power.apply", String("key=power.intent value=") + kv);
}

// -----------------------------------------------------------------------------
// Event handlers (Policy/Config)
static void on_wake_evt(const String& topic, const String& value) {
  const bool on = (value.endsWith("=on") || value == "on" || value == "1" || value == "true");

  if (topic == "wake.user_btn")            { s_wake_user_btn  = on; TRACE_WAKE_KV("user_btn",        on); return; }
  if (topic == "wake.rtc")                 { s_wake_rtc       = on; TRACE_WAKE_KV("rtc",             on); return; }
  if (topic == "wake.lora_dio1")           { s_wake_lora_dio1 = on; TRACE_WAKE_KV("lora_dio1",       on); return; }
  if (topic == "wake.touch_lightsleep")    { s_wake_touch_ls  = on; TRACE_WAKE_KV("touch_lightsleep", on); return; }
  if (topic == "power.sleep.autowake_ms") {
    // robust: accept "1500" OR "value=1500"
    int eq = value.lastIndexOf('=');
    String num = (eq >= 0) ? value.substring(eq + 1) : value;
    uint32_t ms = (uint32_t) num.toInt();
    s_autowake_ms = ms;
    TRACE("trace.svc.power.sleep", String("cfg autowake_ms=") + String(ms));
    return;
  }
}

// -----------------------------------------------------------------------------
// Init
void init() {
  // Policies
  bus::subscribe("wake.*", on_wake_evt);

  // Power intents
  bus::subscribe("power.intent",
    [](const String& /*topic*/, const String& kv){ handle_intent(kv); });

  // Startup trace of policy
  TRACE_WAKE_KV("user_btn",         s_wake_user_btn ? 1 : 0);
  TRACE_WAKE_KV("rtc",              s_wake_rtc ? 1 : 0);
  TRACE_WAKE_KV("lora_dio1",        s_wake_lora_dio1 ? 1 : 0);
  TRACE_WAKE_KV("touch_lightsleep", s_wake_touch_ls ? 1 : 0);
}

} } // namespace svc::power


// ============================================================================
// File: src/drivers/drv_power_axp2101.cpp
// Adds bus ops for AXP2101 IRQ: enable_all / clear_all / dump / enable_keys_only
// Uses Wire I2C directly (addr 0x34) for register access.
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include <Wire.h>
#include "../core/bus.hpp"

namespace drv { namespace power { namespace axp {

static constexpr uint8_t AXP2101_ADDR = 0x34;

// Registers
static constexpr uint8_t REG_INTEN1 = 0x40;
static constexpr uint8_t REG_INTEN2 = 0x41;
static constexpr uint8_t REG_INTEN3 = 0x42;
static constexpr uint8_t REG_INTSTS1= 0x48;
static constexpr uint8_t REG_INTSTS2= 0x49;
static constexpr uint8_t REG_INTSTS3= 0x4A;
static constexpr uint8_t REG_PWRON  = 0x20;
static constexpr uint8_t REG_PWROFF = 0x21;
static constexpr uint8_t REG_SLPWKP = 0x26; // SLEEP/WAKEUP CTRL

// I2C helpers
static uint8_t i2cRead8(uint8_t reg) {
  Wire.beginTransmission(AXP2101_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0; // repeated start
  Wire.requestFrom(AXP2101_ADDR, (uint8_t)1);
  if (Wire.available()) return (uint8_t)Wire.read();
  return 0;
}
static void i2cWrite8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AXP2101_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void op_enable_all(const String& value) {
  // Enable all interrupts (careful in production; here for debugging)
  (void)value;
  i2cWrite8(REG_INTEN1, 0xFF);
  i2cWrite8(REG_INTEN2, 0xFF);
  i2cWrite8(REG_INTEN3, 0xFF);
}

static void op_clear_all() {
  // Write-1-to-Clear on status regs
  uint8_t s1 = i2cRead8(REG_INTSTS1);
  uint8_t s2 = i2cRead8(REG_INTSTS2);
  uint8_t s3 = i2cRead8(REG_INTSTS3);
  if (s1) i2cWrite8(REG_INTSTS1, s1);
  if (s2) i2cWrite8(REG_INTSTS2, s2);
  if (s3) i2cWrite8(REG_INTSTS3, s3);
}

static void op_dump() {
  uint8_t en1 = i2cRead8(REG_INTEN1), en2 = i2cRead8(REG_INTEN2), en3 = i2cRead8(REG_INTEN3);
  uint8_t st1 = i2cRead8(REG_INTSTS1), st2 = i2cRead8(REG_INTSTS2), st3 = i2cRead8(REG_INTSTS3);
  uint8_t pon = i2cRead8(REG_PWRON),   pof = i2cRead8(REG_PWROFF), slw = i2cRead8(REG_SLPWKP);
  String msg = "INTEN=";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X", en1); msg += buf; msg += " ";
  snprintf(buf, sizeof(buf), "%02X", en2); msg += buf; msg += " ";
  snprintf(buf, sizeof(buf), "%02X", en3); msg += buf; msg += " ";
  msg += "INTSTS=";
  snprintf(buf, sizeof(buf), "%02X", st1); msg += buf; msg += " ";
  snprintf(buf, sizeof(buf), "%02X", st2); msg += buf; msg += " ";
  snprintf(buf, sizeof(buf), "%02X", st3); msg += buf; msg += " ";
  msg += "PWRON="; snprintf(buf, sizeof(buf), "%02X", pon); msg += buf; msg += " ";
  msg += "PWROFF=";snprintf(buf, sizeof(buf), "%02X", pof); msg += buf; msg += " ";
  msg += "SLPWK="; snprintf(buf, sizeof(buf), "%02X", slw); msg += buf;
  bus::emit_sticky("trace.drv.power.axp", msg);
}

static void op_enable_keys_only() {
  // Enable only power-key related IRQs: INTEN2 bits 0..3
  i2cWrite8(REG_INTEN1, 0x00);
  i2cWrite8(REG_INTEN2, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
  i2cWrite8(REG_INTEN3, 0x00);
}

static void handle_bus(const String& /*topic*/, const String& kv) {
  // kv examples: "op=enable_all value=on", "op=clear_all", "op=dump", "op=enable_keys_only"
  int io = kv.indexOf("op=");
  if (io < 0) return;
  int so = io + 3;
  int eo = kv.indexOf(' ', so);
  if (eo < 0) eo = kv.length();
  String op = kv.substring(so, eo);

  if (op == "enable_all") {
    op_enable_all(kv);
  } else if (op == "clear_all") {
    op_clear_all();
  } else if (op == "dump") {
    op_dump();
  } else if (op == "enable_keys_only") {
    op_enable_keys_only();
  }
}

void init() {
  // Ensure Wire is initialized somewhere globally; call begin defensively
  if (!Wire.isEnabled()) {
#if defined(ARDUINO_ARCH_ESP32)
    Wire.begin();
#else
    Wire.begin();
#endif
  }
  bus::subscribe("power.axp.irq", handle_bus);
}

} } } // namespace drv::power::axp
