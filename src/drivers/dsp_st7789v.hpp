#pragma once
#include <Arduino.h>

class DSP_ST7789V {
public:
  bool begin() { return true; } // später echte Init
  void sleep() {}
  void wake()  {}
};
