#include "drivers/dsp_st7789v.hpp"

DspST7789V::DspST7789V() : _tft(nullptr), _ok(false), _x_off_base(0), _y_off_base(80), _rotation(0) {}

bool DspST7789V::begin(int mosi, int sclk, int cs, int dc, int rst, uint16_t w, uint16_t h) {
  // HW-SPI (MISO=-1, write-only)
  SPI.begin(sclk, -1, mosi, cs);

  _tft = new ST7789Ex(cs, dc, rst);
  _tft->init(w, h);               // 240x240
  _tft->setSPISpeed(40000000);    // 40 MHz
  _rotation = 0;
  _applyOffsetsForRotation_();    // setzt (0,80) bei Rot=0
  _tft->fillScreen(ST77XX_BLACK);

  _ok = true;
  return true;
}

void DspST7789V::setRotation(uint8_t r) {
  if (!_ok || !_tft) return;
  _rotation = (uint8_t)(r & 3);
  _tft->setRotation(_rotation);
  _applyOffsetsForRotation_();
}

void DspST7789V::setBaseOffsets(int8_t x_off, int8_t y_off) {
  _x_off_base = x_off;
  _y_off_base = y_off;
  _applyOffsetsForRotation_();
}

void DspST7789V::_applyOffsetsForRotation_() {
  if (!_ok || !_tft) return;
  // Für ein quadratisches Panel (240x240) müssen die Controller-Offsets je Rotation
  // auf die Achse gemappt werden. Für (0,80):
  //  Rot0/180:  x=0,  y=80
  //  Rot90/270: x=80, y=0
  int8_t x_off = 0, y_off = 0;
  if (_rotation == 0 || _rotation == 2) {
    x_off = _x_off_base;
    y_off = _y_off_base;
  } else { // 90 oder 270 Grad
    x_off = _y_off_base;
    y_off = _x_off_base;
  }
  _tft->setOffsets(x_off, y_off);
}

void DspST7789V::fillColor(uint16_t color) {
  if (_ok && _tft) _tft->fillScreen(color);
}

void DspST7789V::drawTestGrid() {
  if (!_ok || !_tft) return;

  _tft->fillScreen(ST77XX_BLACK);

  // Rahmen exakt 0..239
  _tft->drawRect(0, 0, 240, 240, ST77XX_WHITE);

  // Raster alle 20px bis Kante (ohne doppelte Randlinie)
  for (int x = 20; x <= 220; x += 20) _tft->drawFastVLine(x, 0, 240, ST77XX_WHITE);
  for (int y = 20; y <= 220; y += 20) _tft->drawFastHLine(0, y, 240, ST77XX_WHITE);

  // Prüfpixel: alle vier Ecken
  _tft->drawPixel(0,   0,   ST77XX_RED);
  _tft->drawPixel(239, 0,   ST77XX_GREEN);
  _tft->drawPixel(0,   239, ST77XX_BLUE);
  _tft->drawPixel(239, 239, ST77XX_YELLOW);
}
