#include "os/touch_service.hpp"

TouchService::TouchService()
: _fingerActive(false),
  _lastEvtMs(0),
  _lastX(0), _lastY(0),
  _rotation(0), _swapXY(false), _invertX(false), _invertY(false),
  _enabled(false),
  _drv(),
  _bus(nullptr),
  _power(nullptr),
  _api(nullptr)
{}

void TouchService::begin(SystemConfig const& cfg) {
  (void)cfg;
  _rotation = 0; _swapXY = false; _invertX = false; _invertY = false;
}

// Kompatibilität: übernimmt vorhandene Call-Sites in main.cpp
bool TouchService::begin(TwoWire& bus, int /*sda*/, int /*scl*/) {
  _bus = &bus;               // Bus wird vom Aufrufer bereits initiiert (Wire.begin(...))
  _enabled = _drv.begin(*_bus, TWATCH_S3_TOUCH_Pins::I2C_ADDR, TWATCH_S3_TOUCH_Pins::INT);
  return _enabled;
}

void TouchService::attachPower(PowerService& pwr) { _power = &pwr; }
void TouchService::attachApi(ApiBus& api) { _api = &api; (void)_api; }

void TouchService::enable() {
  if (_enabled) return;

  // Interner Pfad: falls kein Bus gesetzt wurde (neue Nutzung), nutze Wire1 (physikalischer Touch-Bus)
  if (_bus == nullptr) {
    Wire1.begin(TWATCH_S3_I2C1::SDA, TWATCH_S3_I2C1::SCL, TWATCH_S3_I2C1::FREQ_HZ);
    Wire1.setTimeOut(4);
    _bus = &Wire1;
  }

  _enabled = _drv.begin(*_bus, TWATCH_S3_TOUCH_Pins::I2C_ADDR, TWATCH_S3_TOUCH_Pins::INT);
}

void TouchService::disable() {
  if (!_enabled) return;
  _drv.end();
  _enabled = false;
  _fingerActive = false;
}

void TouchService::simulateTap(uint16_t x, uint16_t y) {
  _onDown(x,y,0);
  _onUp(x,y,0);
}

void TouchService::_applyMapping(uint16_t& x, uint16_t& y) const {
  // Neutral; später via cfg dynamisch
  if (_swapXY) { uint16_t t=x; x=y; y=t; }
  if (_invertX) x = 239 - x;
  if (_invertY) y = 239 - y;

  if (_rotation == 90)       { uint16_t nx=y;      uint16_t ny=239-x; x=nx; y=ny; }
  else if (_rotation == 180) { x = 239 - x;        y = 239 - y; }
  else if (_rotation == 270) { uint16_t nx=239-y;  uint16_t ny=x;     x=nx; y=ny; }

  if (x > 239) x = 239;
  if (y > 239) y = 239;
}

void TouchService::_onDown(uint16_t x, uint16_t y, uint8_t /*id*/) {
  _applyMapping(x,y);
  _fingerActive = true;
  _lastX = x; _lastY = y;
  _lastEvtMs = millis();
  if (_power) _power->userActivity(PowerService::Activity::Touch);
}

void TouchService::_onMove(uint16_t x, uint16_t y, uint8_t /*id*/) {
  _applyMapping(x,y);
  _lastX = x; _lastY = y;
  _lastEvtMs = millis();
  if (_power) _power->userActivity(PowerService::Activity::Touch);
}

void TouchService::_onUp(uint16_t x, uint16_t y, uint8_t /*id*/) {
  _applyMapping(x,y);
  _fingerActive = false;
  _lastX = x; _lastY = y;
  _lastEvtMs = millis();
}

void TouchService::loop() {
  if (!_enabled) return;

  if (_drv.hasPending()) {
    TCH_FT6236U::Report rep;
    if (_drv.readReport(rep)) {
      if (rep.count == 0) {
        if (_fingerActive) _onUp(_lastX, _lastY, 0);
      } else {
        const auto& p = rep.pts[0];
        uint16_t x = p.x, y = p.y;
        switch (p.event) {
          case 0: _onDown(x,y,p.id); break;       // down
          case 1: _onUp(x,y,p.id);   break;       // up
          case 2: default:
            if (!_fingerActive) _onDown(x,y,p.id);
            else                _onMove(x,y,p.id);
            break;
        }
      }
    } else {
      _drv.clearPending();
    }
  }
}
