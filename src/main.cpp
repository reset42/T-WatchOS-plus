#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>

#include "drivers/board_pins.hpp"
#include "drivers/pmu_axp2101.hpp"
#include "os/system_config.hpp"
#include "os/power_service.hpp"
#include "os/display_service.hpp"
#include "os/touch_service.hpp"
#include "os/api_bus.hpp"
#include "os/power_api.hpp"
#include "os/api_transport_serial.hpp"

// --- Globals ---
SystemConfig    g_cfg;
DevConfig       g_dev;
PMU_AXP2101     g_pmu;
PowerService    g_power;
DisplayService  g_display;
TouchService    g_touch;

ApiBus          g_api;
SerialTransport g_serial;

static String g_rx;

struct DebugCtl {
  bool     pmu_events = false;
  uint32_t telem_ms   = 0;
} g_dbg;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[BOOT] T-Watch S3 — PMU AXP2101 + PowerService + API");

  if (!LittleFS.begin(true)) {
    Serial.println("[E] LittleFS.begin() failed");
  }

  // Korrekte Pfade (Root des FS):
  g_cfg.load(LittleFS, "/config/system.conf");
  g_dev.load(LittleFS, "/config/dev.conf");

  // I2C0: PMU/Display
  Wire.begin(TWATCH_S3_I2C0::SDA, TWATCH_S3_I2C0::SCL, TWATCH_S3_I2C0::FREQ_HZ);

  // PMU + Power
  if (!g_pmu.begin(Wire, 0x34, TWATCH_S3_PMU_Pins::PMU_IRQ)) {
    Serial.println("[E] PMU begin failed");
  }
  g_power.begin(g_cfg, &g_pmu);
  g_power.attachApi(g_api);

  // Display
  g_display.begin(&g_pmu, g_dev, g_cfg);

  // Touch nicht sofort starten → Boot-Guard (siehe loop()).
  g_touch.begin(g_cfg);
  g_touch.attachPower(g_power);
  g_touch.attachApi(g_api);
}

void loop() {
  // Serial → ApiBus
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { g_api.ingestLine(g_rx, &g_serial); g_rx = ""; }
    else if (c != '\r') { g_rx += c; }
  }

  // PMU-IRQ
  PMU_AXP2101::Event e;
  while (g_pmu.popEvent(e)) {
    g_power.onPmuEvent(e);
    if (g_dbg.pmu_events) {
      g_api.publishEvent("pmu/irq", {
        {"type", PMU_AXP2101::evtName(e.type)},
        {"ts_ms", String(e.ts_ms)}
      });
    }
  }

  // Boot-Guard: Touch erst nach 2000 ms aktivieren
  static bool touch_started = false;
  static uint32_t t0 = millis();
  if (!touch_started && (millis() - t0 >= 2000)) {
    // I2C1 + Probe intern, IRQ erst nach erfolgreichem Probe
    g_touch.enable();
    touch_started = true;
    g_api.publishEvent("touch/info", {{"state","enabled_after_boot_guard"}});
  }

  g_touch.loop();
  g_power.loop();

  // optionale Telemetrie
  static uint32_t lastT = 0;
  const uint32_t now = millis();
  if (g_dbg.telem_ms && (now - lastT >= g_dbg.telem_ms)) {
    lastT = now;
    auto t = g_pmu.readTelemetry();
    g_api.publishEvent("pmu/telemetry", {
      {"batt_mV",  String((unsigned)t.batt_mV)},
      {"batt_pct", String((unsigned)t.batt_percent)},
      {"sys_mV",   String((unsigned)t.sys_mV)},
      {"vbus_mV",  String((unsigned)t.vbus_mV)},
      {"charging", String((int)t.charging)}
    });
  }

  delay(5);
}
