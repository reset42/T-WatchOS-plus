// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
#include "service_display.hpp"
#include "../core/bus.hpp"
#include "../drivers/drv_display_st7789v.hpp"

namespace svc { namespace display {

// --- internes Hilfslog (nicht persistent) ---
static inline void TRACE_IGN(const String& key, const String& value, const char* reason) {
  bus::emit_sticky(String("trace.svc.display.ignored"),
                   String("key=") + key + " value=" + value + " reason=" + reason);
}

// Whitelist-Forwarder zum Treiber
static void forward_to_driver(const String& topic, const String& value) {
  drv::display_st7789v::apply_kv(topic, value);
}

void init() {
  // Treiber initialisieren (SPI/PWM + Panel-Setup fix verdrahtet)
  drv::display_st7789v::init();

  // UI-Helligkeit (%): akzeptiert "value=NN" oder "NN"
  bus::subscribe("ui.brightness", [](const String& topic, const String& value){
    String v = value;
    int eq = v.indexOf('=');
    if (eq >= 0) v = v.substring(eq + 1);
    int pct = v.toInt();
    drv::display_st7789v::set_brightness_pct((uint8_t)pct);
  });

  // Backlight-Parameter (Timer Hz / Auflösung / Gamma / Min%)
  bus::subscribe("backlight.*", forward_to_driver);

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

  // power.mode_changed wird vom gehärteten Displaytreiber nicht mehr benötigt
  // → kein Subscribe mehr, um unnötige Bus-Last zu vermeiden
}

} } // namespace svc::display
