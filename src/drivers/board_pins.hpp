#pragma once
#include <Arduino.h>

/*
 * LilyGO T-Watch S3 — zentrale Pinbelegung
 * Rails/Power bleiben exklusiv beim PowerService/PMU.
 */

// -------------------------------
// I2C-Bus 0 (System/PMU/Display)
// -------------------------------
namespace TWATCH_S3_PMU_Pins {
  static constexpr int I2C0_SDA = 10;
  static constexpr int I2C0_SCL = 11;
  static constexpr int PMU_IRQ  = 21; // AXP2101 INT (open-drain, active-low)
}
namespace TWATCH_S3_I2C0 {
  static constexpr int SDA = 10;
  static constexpr int SCL = 11;
  static constexpr uint32_t FREQ_HZ = 400000;
}

// -------------------------------
// I2C-Bus 1 (Touch-Controller)
// -------------------------------
namespace TWATCH_S3_I2C1 {
  static constexpr int SDA = 39;
  static constexpr int SCL = 40;
  static constexpr uint32_t FREQ_HZ = 400000;
}

// -------------------------------
// Display (ST7789V SPI)
// -------------------------------
namespace TWATCH_S3_TFT_Pins {
  static constexpr int MOSI = 13;
  static constexpr int SCLK = 18;
  static constexpr int CS   = 12;
  static constexpr int DC   = 38;
  static constexpr int RST  = -1; // SW-Reset
  static constexpr int BL   = 45; // Backlight GPIO (zunächst nur ON/OFF)
}

// -------------------------------
// Touch-Panel (FT6236U @ 0x38)
// -------------------------------
namespace TWATCH_S3_TOUCH_Pins {
  static constexpr int INT      = 16;   // active-low (level)
  static constexpr uint8_t I2C_ADDR = 0x38;
}
