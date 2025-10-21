// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
// Config-Service ohne Merge-Logik. Lädt /config/dev.ini und /config/user.ini,
// primed ALLE Key/Value-Paare als Sticky-Events: <section>.<key>  value=<raw>
// Schreibt ausschließlich /config/user.ini bei Änderungen (z.B. ui.brightness).

#include "service_config.hpp"
#include <FS.h>
#include <LittleFS.h>
#include "../core/bus.hpp"

namespace config {

static bool  s_dirty = false;
static bool  s_has_ui_brightness = false;
static int   s_ui_brightness = 50;

// ------------------------- Helpers -------------------------
static bool ensure_dir(const char* path) {
  // LittleFS mkdir: existiert bereits -> true
  return LittleFS.mkdir(path) || LittleFS.exists(path);
}

static void prime_kv(const String& section, const String& key, const String& rawValue) {
  if (!section.length() || !key.length()) return;
  // Topic: section.key  | Payload: value=<raw>
  String topic = section + "." + key;
  String kv = "value=" + rawValue;
  bus::emit_sticky(topic, kv);
}

// Parser für INI-Dateien: sehr einfach (Sections, key=value, ;/# Kommentare)
static void parse_and_prime_ini(const char* filepath, bool also_update_user_state) {
  if (!LittleFS.exists(filepath)) return;
  File f = LittleFS.open(filepath, "r");
  if (!f) return;

  String section;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line.startsWith(";") || line.startsWith("#")) continue;

    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.substring(1, line.length() - 1);
      section.trim();
      continue;
    }

    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq); key.trim();
    String val = line.substring(eq + 1); val.trim();

    // 1) Immer: Prime als Sticky
    prime_kv(section, key, val);

    // 2) Optional: user.ini-spezifische State-Übernahmen (bekannte Keys)
    if (also_update_user_state && section == "ui" && key == "brightness") {
      int v = val.toInt();
      if (v < 0) v = 0; if (v > 100) v = 100;
      s_ui_brightness = v;
      s_has_ui_brightness = true;
      // Nicht dirty – Laden ist kein Änderungsgrund
    }
  }
  f.close();
}

static bool write_user_ini(size_t* bytes_written) {
  if (bytes_written) *bytes_written = 0;

  if (!ensure_dir("/config")) {
    return false;
  }

  File f = LittleFS.open("/config/user.ini", "w");
  if (!f) return false;

  size_t n = 0;
  n += f.print("; TwatchOS+ user settings (auto-saved)\n");
  if (s_has_ui_brightness) {
    n += f.print("[ui]\n");
    n += f.print("brightness = ");
    n += f.print(String(s_ui_brightness));
    n += f.print("\n");
  }
  f.close();

  if (bytes_written) *bytes_written = n;
  return true;
}

// ------------------------- Public API -------------------------
void init() {
  // dev.ini zuerst (Hardware-/System-Defaults), read-only
  parse_and_prime_ini("/config/dev.ini", /*also_update_user_state=*/false);

  // user.ini danach (User-Overrides & Präferenzen)
  parse_and_prime_ini("/config/user.ini", /*also_update_user_state=*/true);

  // Laden setzt dirty NICHT
  s_dirty = false;
}

void note_ui_brightness(int value) {
  s_has_ui_brightness = true;
  if (s_ui_brightness != value) {
    s_ui_brightness = value;
    s_dirty = true;
  }
}

bool is_dirty() { return s_dirty; }

String snapshot() {
  String s;
  if (s_has_ui_brightness) {
    if (s.length()) s += " ";
    s += "ui.brightness="; s += String(s_ui_brightness);
  }
  return s;
}

bool save_now(size_t* bytes_written) {
  if (!s_dirty) { if (bytes_written) *bytes_written = 0; return true; }
  size_t bw = 0;
  bool ok = write_user_ini(&bw);
  if (ok) {
    s_dirty = false;
    if (bytes_written) *bytes_written = bw;
  }
  return ok;
}

void on_power_last_call() {
  if (!s_dirty) return;
  size_t bw = 0;
  (void)save_now(&bw);
  // Optional: Event "evt config.saved bytes=<bw>" denkbar
}

bool has_ui_brightness() { return s_has_ui_brightness; }
int  get_ui_brightness() { return s_ui_brightness; }

} // namespace config
