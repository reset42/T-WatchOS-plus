#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>

#include "drivers/board_pins.hpp"
#include "drivers/pmu_axp2101.hpp"
#include "os/system_config.hpp"
#include "os/power_service.hpp"
#include "os/display_service.hpp"
#include "os/touch_service.hpp"
#include "os/bus_guard.hpp"

// zentrale API
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

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[BOOT] init...");

  if (!LittleFS.begin(true)) {
    Serial.println("[E] LittleFS.begin() failed");
  }
  g_cfg.load(LittleFS, "/config/system.conf");
  g_dev.load(LittleFS, "/config/dev.conf");

  // BusGuard zuerst
  g_bus.begin();

  // I2C0
  Wire.begin(TWATCH_S3_I2C0::SDA, TWATCH_S3_I2C0::SCL, TWATCH_S3_I2C0::FREQ_HZ);

  // PMU & Power (IRQ an)
  if (!g_pmu.begin(Wire, 0x34, TWATCH_S3_PMU_Pins::PMU_IRQ)) {
    Serial.println("[E] PMU begin failed");
  }

  // Dev in User-View ablegen (einige Services erwarten cfg.dev.*)
  g_cfg.dev = g_dev;

  g_power.begin(g_cfg, &g_pmu);
  g_power.attachDisplay(g_display);

  // API verdrahten
  g_api.attach(&g_serial);
  g_power.attachApi(g_api);
  bindPowerApi(g_power, g_api);
  g_bus.attachApi(g_api);

  // Display
  g_display.begin(&g_pmu, g_dev, g_cfg);
  g_display.attachApi(g_api);

  // Touch (Bus1 init erfolgt beim enable)
  g_touch.begin(g_cfg);
  g_touch.attachPower(g_power);
  g_touch.attachApi(g_api);
  g_touch.enable();

  Serial.println("[BOOT] ready");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { g_rx.trim(); if (g_rx.length()) g_api.ingestLine(g_rx, &g_serial); g_rx = ""; }
    else if (c != '\r') { g_rx += c; }
  }

  PMU_AXP2101::Event e;
  while (g_pmu.popEvent(e)) {
    g_power.onPmuEvent(e);
  }

  g_touch.loop();
  g_power.loop();
  g_display.loop();

  delay(2);
}
