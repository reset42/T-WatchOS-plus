// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
#include "service_display.hpp"
#include "../core/bus.hpp"
#include "../drivers/drv_display_st7789v.hpp"

namespace svc { namespace display {

// Cache der letzten PWM-Settings (aus Config-Stickies), um Reset-Effekte im Treiber zu neutralisieren.
static int s_pwm_hz   = -1;
static int s_pwm_bits = -1;

// --- internes Hilfslog (nicht persistent) ---
static inline void TRACE_IGN(const String& key, const String& value, const char* reason) {
  bus::emit_sticky(String("trace.svc.display.ignored"),
                   String("key=") + key + " value=" + value + " reason=" + reason);
}

// Whitelist-Forwarder zum Treiber
static void forward_to_driver(const String& topic, const String& value) {
  drv::display_st7789v::apply_kv(topic, value);
}

// Kleiner Helfer: Key-Value "value=NN" → int
static int kv_to_int(const String& v) {
  int eq = v.indexOf('=');
  String s = (eq >= 0) ? v.substring(eq + 1) : v;
  return s.toInt();
}

void init() {
  // Treiber initialisieren (SPI/PWM + Panel-Setup fix verdrahtet)
  drv::display_st7789v::init();

  // Backlight-Parameter (Timer Hz / Auflösung / Gamma / Min%)
  bus::subscribe("backlight.*", [](const String& topic, const String& value){
    if (topic == "backlight.pwm_timer_hz")  { s_pwm_hz   = kv_to_int(value); }
    if (topic == "backlight.pwm_resolution_bits") { s_pwm_bits = kv_to_int(value); }
    forward_to_driver(topic, value);
  });

  // SPI-„Profil“-Keys (nur Telemetrie im Treiber)
  bus::subscribe("spi0.*", [](const String& topic, const String& value){
    // Treiber akzeptiert nur slice_ms / prio / role → Rest ignorieren
    if (topic == "spi0.slice_ms" || topic == "spi0.prio" || topic == "spi0.role") {
      forward_to_driver(topic, value);
    } else {
      TRACE_IGN(topic, value, "unsupported_spi_key");
    }
  });

  // Display-Befehle: nur zulässige Keys weiterreichen
  bus::subscribe("display.*", [](const String& topic, const String& value){
    // Hart verdrahtet im Treiber → nicht weiterleiten
    if (topic == "display.colmod" ||
        topic == "display.rgb565_endian" ||
        topic == "display.spi_mode" ||
        topic == "display.spi_hz" ||
        topic == "display.color_order" ||
        topic == "display.invert") {
      TRACE_IGN(topic, value, "hardwired_in_driver");
      return;
    }

    // Erlaubte Keys
    if (topic == "display.rotate" ||
        topic == "display.fill" ||
        topic == "display.test" ||
        topic == "display.offset.rot0" ||
        topic == "display.offset.rot1" ||
        topic == "display.offset.rot2" ||
        topic == "display.offset.rot3") {
      forward_to_driver(topic, value);
      return;
    }

    // Alles andere: ignorieren (sicher)
    TRACE_IGN(topic, value, "unsupported_display_key");
  });

  // UI-Helligkeit (%): akzeptiert "value=NN" oder "NN"
  bus::subscribe("ui.brightness", [](const String& /*topic*/, const String& value){
    // Erst Helligkeit setzen ...
    String v = value;
    int eq = v.indexOf('=');
    if (eq >= 0) v = v.substring(eq + 1);
    int pct = v.toInt();
    drv::display_st7789v::set_brightness_pct((uint8_t)pct);

    // ... danach PWM-Config re-asserten, falls der Treiber beim Duty-Setup die Auflösung umstellt.
    if (s_pwm_hz   > 0) drv::display_st7789v::apply_kv("backlight.pwm_timer_hz",       String("value=") + String(s_pwm_hz));
    if (s_pwm_bits > 0) drv::display_st7789v::apply_kv("backlight.pwm_resolution_bits",String("value=") + String(s_pwm_bits));
  });

  // power.mode_changed wird vom gehärteten Displaytreiber nicht mehr benötigt
  // → kein Subscribe mehr, um unnötige Bus-Last zu vermeiden
}

} } // namespace svc::display
