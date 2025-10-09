#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ST7789 240x240 auf Controllern mit 240x320-RAM braucht oft ein Fenster-Offset (z.B. 0,80).
// Dieser Wrapper kapselt das und passt die Offsets bei Rotation automatisch an.

class DspST7789V {
public:
  DspST7789V();

  // Pins: HW-SPI (MOSI,SCLK), CS, DC, SW-Reset (=-1)
  bool begin(int mosi, int sclk, int cs, int dc, int rst, uint16_t w, uint16_t h);

  // Rotation 0/1/2/3 (0°, 90°, 180°, 270°)
  void setRotation(uint8_t r);

  // Setzt das Basis-Fenster (Controller-RAM -> sichtbares Panel).
  // Für T-Watch S3: (0,80) → volle 240x240 sichtbar.
  void setBaseOffsets(int8_t x_off, int8_t y_off);

  void fillColor(uint16_t color);
  void drawTestGrid();
  bool ok() const { return _ok; }

private:
  // Subklasse, um protected setColRowStart() öffentlich zu machen
  class ST7789Ex : public Adafruit_ST7789 {
  public:
    using Adafruit_ST7789::Adafruit_ST7789;
    void setOffsets(int8_t col, int8_t row) { setColRowStart(col, row); }
  };

  void _applyOffsetsForRotation_(); // berechnet Offsets anhand Rotation & Basis

  ST7789Ex* _tft;
  bool _ok;

  // Basis-Offsets (vor Rotation)
  int8_t _x_off_base = 0;
  int8_t _y_off_base = 80; // <- wichtig für viele 240x240-Panels (T-Watch S3)
  uint8_t _rotation = 0;
};
