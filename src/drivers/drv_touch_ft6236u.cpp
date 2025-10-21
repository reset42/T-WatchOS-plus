// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
#include "drv_touch_ft6236u.hpp"
#include "../core/bus.hpp"
#include <Wire.h>

namespace drv { namespace touch_ft6236u {

static bool s_irq_on = true;
static bool s_active = true;

// Minimal-Telemetrie-Wrapper
static inline void TRACE(const char* topic, const String& msg){
  bus::emit_sticky(String(topic), msg);
}

// Hier ggf. echte FT6236U-Register ansteuern (Power/Sleep/IRQ-Enable)
// Aktuell nur Traces, um Bus-Flow zu verifizieren.
static void hw_power_active(){
  // TODO: echte Reg-Writes (z.B. MODE=ACTIVE)
  s_active = true;
  TRACE("trace.drv.touch.apply", "key=touch.power value=active");
}
static void hw_power_sleep(){
  // TODO: echte Reg-Writes (z.B. MODE=SLEEP)
  s_active = false;
  TRACE("trace.drv.touch.apply", "key=touch.power value=sleep");
}
static void hw_irq(bool en){
  // TODO: GPIO/INT Mask setzen
  s_irq_on = en;
  TRACE("trace.drv.touch.apply", String("key=touch.irq value=") + (en?"on":"off"));
}

// I2C-Härtung / Forward-Keys
static uint16_t s_i2c_timeout_ms = 25;
static uint8_t  s_i2c_retry      = 2;

void init(){
  // Annahme: Wire bereits global init; sonst hier begin() aufrufen.
  // IRQ-Pin/GPIO Setup wäre hier sinnvoll, falls vorhanden.
  TRACE("trace.drv.touch.init", "ok=1 i2c1=1 ft6336u_ack=1");
}

void apply_kv(const String& key, const String& value){
  if (key == "touch.power") {
    if (value == "active") hw_power_active();
    else                   hw_power_sleep();
    return;
  }
  if (key == "touch.irq") {
    hw_irq(value == "on");
    return;
  }

  // I2C-Härtung (nur Traces; echte Anwendung je nach Low-Level)
  if (key == "i2c0.timeout_ms") {
    long t = value.toInt(); if (t < 1) t = 1; if (t > 1000) t = 1000;
    s_i2c_timeout_ms = (uint16_t)t;
    TRACE("trace.drv.touch.apply", String("key=i2c0.timeout_ms value=")+String((int)s_i2c_timeout_ms));
    return;
  }
  if (key == "i2c0.retry") {
    long t = value.toInt(); if (t < 0) t = 0; if (t > 10) t = 10;
    s_i2c_retry = (uint8_t)t;
    TRACE("trace.drv.touch.apply", String("key=i2c0.retry value=")+String((int)s_i2c_retry));
    return;
  }
}

} } // namespace drv::touch_ft6236u
