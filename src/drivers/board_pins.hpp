#pragma once
#include <Arduino.h>

/*
 * LilyGO T-Watch S3 — zentrale Pinbelegung
 *
 * Leitlinien:
 * - Nur Pin-/Address-Definitionen. Keine Rail-Steuerung, kein Wire.begin() hier.
 * - Rails/Power bleiben exklusiv beim PowerService/PMU.
 * - Treiber (Display, Touch, etc.) ziehen ihre Pins ausschließlich über dieses Header.
 *
 * Kompatibilität:
 * - Beibehalt der bestehenden Namen aus dem Projekt (TWATCH_S3_PMU_Pins::I2C0_SDA/SCL,
 *   TWATCH_S3_TFT_Pins::BL, …), zusätzlich optionale neue Namespaces für Busse.
 */

// -------------------------------
// I2C-Bus 0 (System/PMU/Display)
// -------------------------------
namespace TWATCH_S3_PMU_Pins {
  // Bestehende, im Projekt genutzte Alias-Namen
  static constexpr int I2C0_SDA = 10;
  static constexpr int I2C0_SCL = 11;
  static constexpr int PMU_IRQ  = 21; // AXP2101 INT (open-drain, active-low)
}

// Optionale, klarere Bus-Bezeichner (zusätzlich zu obigen Aliasen)
namespace TWATCH_S3_I2C0 {
  static constexpr int SDA = 10;
  static constexpr int SCL = 11;
  static constexpr uint32_t FREQ_HZ = 400000; // 400 kHz
}

// -------------------------------
// I2C-Bus 1 (Touch-Controller)
// -------------------------------
namespace TWATCH_S3_I2C1 {
  static constexpr int SDA = 39;
  static constexpr int SCL = 40;
  static constexpr uint32_t FREQ_HZ = 400000; // 400 kHz
}

// -------------------------------
// Display (ST7789 SPI) + Backlight
// -------------------------------
namespace TWATCH_S3_TFT_Pins {
  static constexpr int MOSI = 13;
  static constexpr int SCLK = 18;
  static constexpr int CS   = 12;
  static constexpr int DC   = 38;
  static constexpr int BL   = 45; // Backlight (LEDC attach)
  static constexpr int RST  = -1; // meist unbeschaltet → -1
}

// -------------------------------
// Touch-Panel (FT6236U @ 0x38)
// -------------------------------
namespace TWATCH_S3_TOUCH_Pins {
  static constexpr int INT      = 16;   // active-low (level)
  static constexpr uint8_t I2C_ADDR = 0x38; // 7-bit
  // Hinweis: physikalisch an I2C1 (SDA=39/SCL=40). Main kann aber (noch) I2C0 aufrufen.
}

// -------------------------------
// Sonstiges (BMA423 etc. – unverändert, falls benötigt)
// -------------------------------
namespace TWATCH_S3_BMA423_Pins {
  static constexpr int INT = 14;
  static constexpr int SDA = 10;
  static constexpr int SCL = 11;
}
