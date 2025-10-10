#include "os/bus_guard.hpp"
#include "os/api_bus.hpp"
#include <Wire.h>
#include <string.h>

// Globale Instanz
BusGuard g_bus;

BusGuard::BusGuard()
: _mtx_i2c0(nullptr),
  _mtx_i2c1(nullptr),
  _mtx_spi_lcd(nullptr),
  _i2c0_locked(false),
  _i2c1_locked(false),
  _spi_locked(false)
{
  _owner_i2c0[0] = 0;
  _owner_i2c1[0] = 0;
  _owner_spi [0] = 0;
}

void BusGuard::begin() {
  if (!_mtx_i2c0) _mtx_i2c0 = xSemaphoreCreateMutex();
  if (!_mtx_i2c1) _mtx_i2c1 = xSemaphoreCreateMutex();
  if (!_mtx_spi_lcd) _mtx_spi_lcd = xSemaphoreCreateMutex();
}

const char* BusGuard::busName(BusId b) {
  switch (b) {
    case BusId::I2C0:    return "i2c0";
    case BusId::I2C1:    return "i2c1";
    case BusId::SPI_LCD: return "spi_lcd";
    case BusId::SPI_LORA:return "spi_lora";
    default:             return "unknown";
  }
}

bool BusGuard::parseBus(const String& s, BusId& out) {
  if (s.equalsIgnoreCase("i2c0"))      { out = BusId::I2C0; return true; }
  if (s.equalsIgnoreCase("i2c1"))      { out = BusId::I2C1; return true; }
  if (s.equalsIgnoreCase("spi") || s.equalsIgnoreCase("spi_lcd")) { out = BusId::SPI_LCD; return true; }
  if (s.equalsIgnoreCase("spi_lora"))  { out = BusId::SPI_LORA; return true; }
  return false;
}

// ---------------- Basic Locks (kompatibel zu bestehendem Code) ----------------
bool BusGuard::lockI2C0(TickType_t to_ticks) {
  if (!_mtx_i2c0) return true;
  if (xSemaphoreTake(_mtx_i2c0, to_ticks) == pdTRUE) { _i2c0_locked = true; return true; }
  return false;
}
void BusGuard::unlockI2C0() {
  if (_mtx_i2c0) { _i2c0_locked = false; xSemaphoreGive(_mtx_i2c0); }
}

bool BusGuard::lockI2C1(TickType_t to_ticks) {
  if (!_mtx_i2c1) return true;
  if (xSemaphoreTake(_mtx_i2c1, to_ticks) == pdTRUE) { _i2c1_locked = true; return true; }
  return false;
}
void BusGuard::unlockI2C1() {
  if (_mtx_i2c1) { _i2c1_locked = false; xSemaphoreGive(_mtx_i2c1); }
}

bool BusGuard::lockSPILcd(TickType_t to_ticks) {
  if (!_mtx_spi_lcd) return true;
  if (xSemaphoreTake(_mtx_spi_lcd, to_ticks) == pdTRUE) { _spi_locked = true; return true; }
  return false;
}
void BusGuard::unlockSPILcd() {
  if (_mtx_spi_lcd) { _spi_locked = false; xSemaphoreGive(_mtx_spi_lcd); }
}

// ---------------- Owned Locks (für Owner-Anzeige) ----------------
void BusGuard::_setOwner_(BusId b, const char* name) {
  const char* src = (name && *name) ? name : "none";
  char* dst = nullptr;
  switch (b) {
    case BusId::I2C0:    dst = _owner_i2c0; break;
    case BusId::I2C1:    dst = _owner_i2c1; break;
    case BusId::SPI_LCD: dst = _owner_spi;  break;
    default: return;
  }
  strncpy(dst, src, OWNER_MAX-1);
  dst[OWNER_MAX-1] = 0;
}
const char* BusGuard::_getOwner_(BusId b) const {
  switch (b) {
    case BusId::I2C0:    return *_owner_i2c0 ? _owner_i2c0 : "none";
    case BusId::I2C1:    return *_owner_i2c1 ? _owner_i2c1 : "none";
    case BusId::SPI_LCD: return *_owner_spi  ? _owner_spi  : "none";
    default:             return "none";
  }
}

bool BusGuard::lockOwned(BusId bus, const char* owner, TickType_t to_ticks) {
  bool ok = false;
  switch (bus) {
    case BusId::I2C0:    ok = lockI2C0(to_ticks); break;
    case BusId::I2C1:    ok = lockI2C1(to_ticks); break;
    case BusId::SPI_LCD: ok = lockSPILcd(to_ticks); break;
    default: return false;
  }
  if (ok) _setOwner_(bus, owner);
  return ok;
}

void BusGuard::unlockOwned(BusId bus, const char* /*owner*/) {
  // Owner wird beim Unlock auf "none" gesetzt (einfach & robust)
  switch (bus) {
    case BusId::I2C0:    unlockI2C0();    break;
    case BusId::I2C1:    unlockI2C1();    break;
    case BusId::SPI_LCD: unlockSPILcd();  break;
    default: return;
  }
  _setOwner_(bus, "none");
}

// ---------------- I2C Scan ----------------
bool BusGuard::i2cScan(BusId bus, String& out_hexlist) {
  TwoWire* w = nullptr;
  if (bus == BusId::I2C0) w = &Wire;
#if CONFIG_IDF_TARGET_ESP32S3
  if (bus == BusId::I2C1) w = &Wire1;
#endif
  if (!w) return false;

  const TickType_t TO = pdMS_TO_TICKS(50);
  bool got = (bus == BusId::I2C0) ? lockI2C0(TO) : lockI2C1(TO);
  if (!got) return false;

  String list;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    w->beginTransmission(addr);
    uint8_t err = (uint8_t)w->endTransmission(true);
    if (err == 0) {
      if (list.length()) list += ",";
      char buf[6]; // "0xZZ"
      sprintf(buf, "0x%02X", addr);
      list += buf;
    }
  }

  if (bus == BusId::I2C0) unlockI2C0(); else unlockI2C1();
  out_hexlist = list;
  return true;
}

// ---------------- API Handlers ----------------
void BusGuard::attachApi(ApiBus& api) { _registerApi_(api); }

void BusGuard::_registerApi_(ApiBus& api) {
  api.registerHandler("bus", [this, &api](const ApiRequest& r) {
    const String& act = r.action;

    if (act == "scan") {
      const String* b = ApiBus::findParam(r.params, "bus");
      if (!b) { api.replyErr(r.origin, "param", "missing bus=i2c0|i2c1"); return; }
      BusId id;
      if (!parseBus(*b, id) || (id != BusId::I2C0 && id != BusId::I2C1)) {
        api.replyErr(r.origin, "param", "invalid bus");
        return;
      }
      String list;
      bool ok = i2cScan(id, list);
      if (!ok) { api.replyErr(r.origin, "busy", "bus locked"); return; }
      api.replyOk(r.origin, {
        {"bus",  String(busName(id))},
        {"addrs", list}
      });
      api.publishEvent("bus/scan", { {"bus", String(busName(id))}, {"addrs", list} });
      return;
    }

    if (act == "status") {
      const String* b = ApiBus::findParam(r.params, "bus");
      if (!b) { api.replyErr(r.origin, "param", "missing bus"); return; }
      BusId id;
      if (!parseBus(*b, id)) { api.replyErr(r.origin, "param", "invalid bus"); return; }
      String locked = "off";
      const char* owner = "none";
      switch (id) {
        case BusId::I2C0:    locked = _i2c0_locked ? "on" : "off"; owner = _getOwner_(id); break;
        case BusId::I2C1:    locked = _i2c1_locked ? "on" : "off"; owner = _getOwner_(id); break;
        case BusId::SPI_LCD: locked = _spi_locked  ? "on" : "off"; owner = _getOwner_(id); break;
        default: break;
      }
      api.replyOk(r.origin, {
        {"bus", String(busName(id))},
        {"locked", locked},
        {"owner", owner}
      });
      return;
    }

    api.replyErr(r.origin, "unknown", "bus.<scan|status>");
  });
}
