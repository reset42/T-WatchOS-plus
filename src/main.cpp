// Minimal: USB-CDC Serial only, LittleFS mount, Event-Bus + A2-Parser.
// Idle-Flush-Konsole mit Prompt. Keine Fremdlibs.

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <stdlib.h>

#include "core/bus.hpp"
#include "core/api_parser.hpp"
#include "services/service_config.hpp"
#include "services/service_power.hpp"
#include "services/service_display.hpp"
#include "services/service_touch.hpp"

static void prompt() { Serial.print(">> "); }
static void outln(const String& s) { Serial.println(s); }

static void outf(const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}

// ----- FS Utils -----
static void listDir(const char *path) {
  File root = LittleFS.open(path);
  if (!root || !root.isDirectory()) { outf("[FS] list fail path=%s\n", path); return; }
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    outf("[FS] %-24s %8u\n", f.path(), (unsigned)f.size());
  }
}
static void dumpFirstLines(const char *path, size_t maxLines = 3) {
  if (!LittleFS.exists(path)) { outf("[FS] missing %s\n", path); return; }
  File f = LittleFS.open(path, "r"); if (!f) { outf("[FS] open fail %s\n", path); return; }
  outf("[FS] open %s size=%u\n", path, (unsigned)f.size());
  String line; size_t n = 0;
  while (f.available() && n < maxLines) { line = f.readStringUntil('\n'); line.trim(); outf("[FS] %s: %s\n", path, line.c_str()); n++; }
  f.close();
}

void setup() {
  // USB-CDC Konsole
  Serial.begin(115200);
  delay(400);

  // LittleFS
  bool mounted = LittleFS.begin(false, "/littlefs", 8, "littlefs");
  outf("[FS] mount=%s\n", mounted ? "ok" : "fail");
  if (mounted) { listDir("/"); listDir("/config"); dumpFirstLines("/config/dev.ini"); dumpFirstLines("/config/user.ini"); }

  // Bus
  bus::init(outln);

  // Config-Service (lädt dev.ini & user.ini, primed Stickies)
  config::init();

  // Start-Stickies (ohne ui.brightness – kommt ggf. aus config.init())
  bus::emit_sticky("power.mode_changed", "mode=ready");
  bus::emit_sticky("time.ready", "epoch=0");
  if (!config::has_ui_brightness()) { bus::emit_sticky("ui.brightness", "value=50"); }

  // Services (orchestrieren Treiber; HW-Zugriffe folgen später im DRV)
  svc::power::init();
  svc::display::init();
  svc::touch::init();

  // Parser
  api::init(outln);

  outln("evt/console mode=log");
  outln("[BOOT] ready");
  prompt();
}

void loop() {
  static String acc;
  static unsigned long last_rx = 0;

  bool any = false;
  while (Serial.available()) {
    any = true;
    char c = (char)Serial.read();
    last_rx = millis();

    if (c == '\r' || c == '\n') {
      if (acc.length()) { String ln = acc; acc = ""; api::handleLine(ln); prompt(); }
    } else if (c == '\b' || c == 0x7F) {
      if (acc.length()) acc.remove(acc.length() - 1);
    } else {
      acc += c;
      if (acc.length() > 240) { outln("err line_too_long"); acc = ""; prompt(); }
    }
  }

  // Idle-Flush
  if (acc.length() && (millis() - last_rx) > 350) {
    String ln = acc; acc = ""; api::handleLine(ln); prompt();
  }

  if (!any) delay(1);
}
