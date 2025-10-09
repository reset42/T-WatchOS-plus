#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ApiBus; // forward
class TwoWire;

enum class BusId : uint8_t {
  I2C0 = 0,
  I2C1 = 1,
  SPI_LCD = 2,
  SPI_LORA = 3
};

class BusGuard {
public:
  BusGuard();

  // Init & API
  void begin();
  void attachApi(ApiBus& api);

  // --------- Basic Locks (bestehende Aufrufe bleiben gültig) ----------
  bool lockI2C0(TickType_t to_ticks);
  void unlockI2C0();

  bool lockI2C1(TickType_t to_ticks);
  void unlockI2C1();

  bool lockSPILcd(TickType_t to_ticks);
  void unlockSPILcd();

  // --------- Owned Locks (für Status/Diagnose) ----------
  bool lockOwned(BusId bus, const char* owner, TickType_t to_ticks);
  void unlockOwned(BusId bus, const char* owner);

  inline bool lockSPILcdOwner(const char* owner, TickType_t to_ticks) {
    return lockOwned(BusId::SPI_LCD, owner, to_ticks);
  }
  inline void unlockSPILcdOwner(const char* owner) {
    unlockOwned(BusId::SPI_LCD, owner);
  }

  // Diag
  static const char* busName(BusId b);
  static bool parseBus(const String& s, BusId& out);

  // Utilities
  bool i2cScan(BusId bus, String& out_hexlist);

private:
  // Mutexes
  SemaphoreHandle_t _mtx_i2c0;
  SemaphoreHandle_t _mtx_i2c1;
  SemaphoreHandle_t _mtx_spi_lcd;

  // State
  volatile bool _i2c0_locked;
  volatile bool _i2c1_locked;
  volatile bool _spi_locked;

  // Owner (nur für Anzeige/Diagnose)
  static constexpr size_t OWNER_MAX = 16;
  char _owner_i2c0[OWNER_MAX];
  char _owner_i2c1[OWNER_MAX];
  char _owner_spi [OWNER_MAX];

  void _setOwner_(BusId b, const char* name);
  const char* _getOwner_(BusId b) const;

  void _registerApi_(ApiBus& api);
};

// globale Instanz
extern BusGuard g_bus;
