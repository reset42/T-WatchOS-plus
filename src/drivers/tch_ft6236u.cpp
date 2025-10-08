#include "drivers/tch_ft6236u.hpp"

bool TCH_FT6236U::begin(TwoWire& bus, uint8_t addr7bit, int irq_gpio) {
  _bus  = &bus;
  _addr = addr7bit;
  _irq  = irq_gpio;
  _pending = false;
  _ok = false;

  if (_irq >= 0) {
    pinMode(_irq, INPUT_PULLUP);
  }

  // --- Erst I2C-Probe, dann IRQ anhängen ---
  uint8_t tmp = 0;
  if (!_readBytes(0x00, &tmp, 1)) return false; // DEV_MODE
  if (!_readBytes(0x02, &tmp, 1)) return false; // TD_STATUS

  if (_irq >= 0) {
    // attach mit Arg; ISR macht nur Flag
    attachInterruptArg((uint8_t)_irq, &_isrThunk, this, FALLING);
  }

  _ok = true;
  return true;
}

void TCH_FT6236U::end() {
  if (_irq >= 0) {
    detachInterrupt((uint8_t)_irq);
  }
  _bus = nullptr;
  _ok = false;
  _pending = false;
}

void IRAM_ATTR TCH_FT6236U::_isrThunk(void* arg) {
  TCH_FT6236U* self = reinterpret_cast<TCH_FT6236U*>(arg);
  if (self) self->_pending = true; // nur Flag, kein I2C im ISR!
}

bool TCH_FT6236U::_readBytes(uint8_t reg, uint8_t* buf, size_t len) {
  if (!_bus) return false;

  _bus->beginTransmission(_addr);
  _bus->write(reg);
  if (_bus->endTransmission(false) != 0) return false; // repeated start

  size_t n = _bus->requestFrom((int)_addr, (int)len);
  if (n != len) return false;

  for (size_t i = 0; i < len; ++i) {
    if (!_bus->available()) return false;
    buf[i] = _bus->read();
  }
  return true;
}

bool TCH_FT6236U::readReport(Report& out) {
  out.count = 0;
  out.pts[0] = {0,0,0,0};
  out.pts[1] = {0,0,1,0};

  if (!_ok || !_bus) return false;

  // 0x00..0x0C lesen (DEV_MODE..P2_YL)
  uint8_t buf[0x0D] = {0};
  if (!_readBytes(0x00, buf, sizeof(buf))) return false;

  uint8_t touches = buf[0x02] & 0x0F; // TD_STATUS
  if (touches > 2) touches = 2;

  auto parsePoint = [&](int base, Point& p){
    uint8_t XH = buf[base+0], XL = buf[base+1];
    uint8_t YH = buf[base+2], YL = buf[base+3];
    p.event = (uint8_t)((XH >> 6) & 0x03);    // 0=down,1=up,2=contact
    p.id    = (uint8_t)((YH >> 4) & 0x0F);
    p.x     = (uint16_t)(((XH & 0x0F) << 8) | XL);
    p.y     = (uint16_t)(((YH & 0x0F) << 8) | YL);
  };

  if (touches >= 1) { parsePoint(0x03, out.pts[0]); out.count = 1; }
  if (touches >= 2) { parsePoint(0x09, out.pts[1]); out.count = 2; }

  _pending = false; // Lesen quittiert INT (Level-IRQ)
  return true;
}
