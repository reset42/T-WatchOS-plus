#pragma once
#include <Arduino.h>
#include "../core/bus.hpp"

// ST7789 command set (subset we use)
#define CMD_NOP        0x00
#define CMD_SWRESET    0x01
#define CMD_SLPOUT     0x11
#define CMD_INVOFF     0x20
#define CMD_INVON      0x21
#define CMD_DISPON     0x29
#define CMD_CASET      0x2A
#define CMD_RASET      0x2B
#define CMD_RAMWR      0x2C
#define CMD_MADCTL     0x36
#define CMD_COLMOD     0x3A

// MADCTL bits
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_BGR 0x08

namespace drv::display_st7789v {

// Panel-Geometry
static constexpr uint16_t PANEL_W = 240;
static constexpr uint16_t PANEL_H = 240;

// Öffentliche Driver-API (genutzt von service_display / diagnostics)
void init();
void apply_kv(const String& key, const String& value);

// Explizite Helfer/Intents (weiter verfügbar, aber farbseitig hart verdrahtet)
void set_brightness_pct(uint8_t pct);     // 0..100 → PWM
void rotate(uint8_t rot);                  // 0..3 (MADCTL + Window)
void set_color_order_rgb(bool rgb_is_true);// Ignoriert zur Laufzeit (hart verdrahtet RGB)
void fill_rgb565(uint16_t rgb565);         // Fullscreen-Fill
void test_pattern(uint8_t which = 1);      // einfacher Diag-Frame

} // namespace drv::display_st7789v
