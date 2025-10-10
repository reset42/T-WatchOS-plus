#include "os/touch_service.hpp"
#include "os/api_bus.hpp"   // für publishEvent()

TouchService::TouchService()
: _downX(0), _downY(0), _downMs(0),
  _fingerActive(false),
  _lastEvtMs(0),
  _lastX(0), _lastY(0),
  _pathDist1(0), _startX(0), _startY(0),
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

bool TouchService::begin(TwoWire& bus, int /*sda*/, int /*scl*/) {
  _bus = &bus;
  _enabled = _drv.begin(*_bus, TWATCH_S3_TOUCH_Pins::I2C_ADDR, TWATCH_S3_TOUCH_Pins::INT);
  return _enabled;
}

void TouchService::attachPower(PowerService& pwr) { _power = &pwr; }
void TouchService::attachApi(ApiBus& api) { _api = &api; }

void TouchService::enable() {
  if (_enabled) return;
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
  if (_swapXY) { uint16_t t=x; x=y; y=t; }
  if (_invertX) x = 239 - x;
  if (_invertY) y = 239 - y;

  if      (_rotation == 90)  { uint16_t nx=y;      uint16_t ny=239-x; x=nx; y=ny; }
  else if (_rotation == 180) { x = 239 - x;        y = 239 - y; }
  else if (_rotation == 270) { uint16_t nx=239-y;  uint16_t ny=x;     x=nx; y=ny; }

  if (x > 239) x = 239;
  if (y > 239) y = 239;
}

void TouchService::_publishRaw(const char* type, uint16_t x, uint16_t y, uint8_t id) {
  if (!_api) return;
  _api->publishEvent("touch/raw", {
    {"type", String(type)},
    {"x",    String(x)},
    {"y",    String(y)},
    {"id",   String(id)}
  });
}

void TouchService::_publishSummary_() {
  if (!_api) return;
  int16_t dx = (int16_t)_lastX - (int16_t)_startX;
  int16_t dy = (int16_t)_lastY - (int16_t)_startY;
  uint32_t dt = millis() - _downMs;

  _api->publishEvent("touch/summary", {
    {"x0",     String(_startX)},
    {"y0",     String(_startY)},
    {"x1",     String(_lastX)},
    {"y1",     String(_lastY)},
    {"dx",     String(dx)},
    {"dy",     String(dy)},
    {"abs_dx", String((uint16_t)(dx < 0 ? -dx : dx))},
    {"abs_dy", String((uint16_t)(dy < 0 ? -dy : dy))},
    {"dist1",  String(_pathDist1)},      // |dx|+|dy| Summen über Moves
    {"dt_ms",  String(dt)}
  });
}

void TouchService::_onDown(uint16_t x, uint16_t y, uint8_t id) {
  _applyMapping(x,y);
  _fingerActive = true;

  _downX = _lastX = _startX = x;
  _downY = _lastY = _startY = y;
  _downMs = _lastEvtMs = millis();
  _pathDist1 = 0;

  if (_power) _power->userActivity(PowerService::Activity::Touch);
  _publishRaw("down", x, y, id);
}

void TouchService::_onMove(uint16_t x, uint16_t y, uint8_t id) {
  _applyMapping(x,y);

  uint32_t now = millis();

  // TAP-LOCK: in den ersten 120ms und im 3px-Radius KEIN move publizieren
  uint16_t dxd = (x > _downX) ? (x - _downX) : (_downX - x);
  uint16_t dyd = (y > _downY) ? (y - _downY) : (_downY - y);
  if ((now - _downMs) <= TAP_LOCK_TIME_MS && dxd <= TAP_LOCK_RADIUS_PX && dyd <= TAP_LOCK_RADIUS_PX) {
    _lastX = x; _lastY = y; _lastEvtMs = now;
    return;
  }

  // De-Dupe (minimale Bewegungen in kurzer Zeit ignorieren)
  uint16_t dxs = (x > _lastX) ? (x - _lastX) : (_lastX - x);
  uint16_t dys = (y > _lastY) ? (y - _lastY) : (_lastY - y);
  if (dxs <= DEDUPE_RADIUS_PX && dys <= DEDUPE_RADIUS_PX && (now - _lastEvtMs) <= DEDUPE_TIME_MS) {
    return;
  }

  _lastX = x; _lastY = y;
  _lastEvtMs = now;

  // Pfadlänge (L1) akkumulieren
  _pathDist1 += (uint32_t)dxs + (uint32_t)dys;

  if (_power) _power->userActivity(PowerService::Activity::Touch);
  _publishRaw("move", x, y, id);
}

void TouchService::_onUp(uint16_t x, uint16_t y, uint8_t id) {
  _applyMapping(x,y);
  _fingerActive = false;
  _lastX = x; _lastY = y;
  _lastEvtMs = millis();

  _publishRaw("up", x, y, id);

  // kompaktes Summary für spätere Gesten/Buttons
  _publishSummary_();
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
