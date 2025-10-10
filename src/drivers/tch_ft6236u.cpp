#include "drivers/tch_ft6236u.hpp"
#include "os/bus_guard.hpp"
#include <freertos/FreeRTOS.h>
#include <Wire.h>

bool TCH_FT6236U::begin(TwoWire& bus, uint8_t addr7bit, int irq_gpio) {
  _bus     = &bus;
  _addr    = addr7bit;
  _irq     = irq_gpio;
  _ok      = false;
  _pending = false;

  if (_irq >= 0) {
    pinMode((gpio_num_t)_irq, INPUT_PULLUP);
    attachInterruptArg((gpio_num_t)_irq, _isrThunk, this, FALLING);
  }

  // einfacher Ping (Chip ID)
  if (!g_bus.lockI2C1(pdMS_TO_TICKS(5))) return false;
  _bus->beginTransmission(_addr);
  _bus->write(0xA8);                 // ID-Reg
  _bus->endTransmission(false);
  _bus->requestFrom((int)_addr, 1);
  uint8_t id = _bus->available() ? _bus->read() : 0x00;
  g_bus.unlockI2C1();

  _ok = (id != 0x00 && id != 0xFF);
  return _ok;
}

void TCH_FT6236U::end() {
  if (_irq >= 0) detachInterrupt((gpio_num_t)_irq);
  _ok = false;
  _pending = false;
}

bool TCH_FT6236U::readReport(Report& out) {
  out.count = 0;
  if (!_ok) { _pending = false; return false; }

  if (!g_bus.lockI2C1(pdMS_TO_TICKS(5))) { _pending = false; return false; }

  // FT6236U: Data ab 0x02 → 1 Finger: 6 Bytes (0x02..0x07)
  _bus->beginTransmission(_addr);
  _bus->write(0x02);
  _bus->endTransmission(false);

  const int N = 6;
  int got = _bus->requestFrom((int)_addr, N);
  if (got == N) {
    uint8_t buf[N];
    for (int i=0;i<N;i++) buf[i] = _bus->read();
    uint8_t event = (buf[0] >> 6) & 0x03;
    uint16_t x = ((uint16_t)(buf[0] & 0x0F) << 8) | (uint16_t)buf[1];
    uint16_t y = ((uint16_t)(buf[2] & 0x0F) << 8) | (uint16_t)buf[3];
    out.count        = 1;
    out.pts[0].event = event; // 0=down, 1=up, 2/3=contact
    out.pts[0].id    = 0;
    out.pts[0].x     = x;
    out.pts[0].y     = y;
  } else {
    out.count = 0;
  }

  g_bus.unlockI2C1();
  _pending = false;
  return true;
}

void IRAM_ATTR TCH_FT6236U::_isrThunk(void* arg) {
  auto self = reinterpret_cast<TCH_FT6236U*>(arg);
  if (!self) return;
  self->_pending = true;
}
