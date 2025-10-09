#pragma once
/**
 * PMU_AXP2101 — thin, IRQ-driven wrapper for XPowers AXP2101
 *
 *  - IRQ-getrieben (keine Polls)
 *  - Schlanke API für den PowerService
 *  - Board-Rails: BL=ALDO2, LoRa=ALDO4+DLDO2
 *
 * Baseline: XPowersLib 0.2.9
 */

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <XPowersLib.h>

class PMU_AXP2101 {
public:
  // ---- Events ---------------------------------------------------------------
  enum class EventType : uint8_t {
    NONE = 0,
    BUTTON_PRESS,
    BUTTON_RELEASE,
    BUTTON_SHORT,
    BUTTON_LONG,
    CHG_START,
    CHG_DONE,
    VBUS_IN,
    VBUS_OUT,
  };

  struct Event {
    EventType type{EventType::NONE};
    uint32_t  ts_ms{0};
  };

  using EventCallback = void(*)(const Event&);

  // ---- Lifecycle ------------------------------------------------------------
  // Achtung: Wire muss vorher mit SDA/SCL initialisiert sein (Wire.begin(...))
  bool begin(TwoWire& bus, uint8_t addr = 0x34, int irq_gpio = -1);
  bool beginDefault(); // I2C0 laut board_pins
  void end();          // detach ISR, IRQs aus, Task killen (kein _pmu.end(), das ist protected)

  // ---- IRQ/Event plumbing ---------------------------------------------------
  void setEventCallback(EventCallback cb) { _cb = cb; }
  bool popEvent(Event &out);

  // ---- Telemetrie Snapshot --------------------------------------------------
  struct Telemetry {
    uint16_t batt_mV{0};
    uint16_t sys_mV{0};
    uint16_t vbus_mV{0};
    uint8_t  batt_percent{0};
    bool     charging{false};
  };
  Telemetry readTelemetry();

  // ---- Charger / VBUS Policies (safe wrappers) ------------------------------
  bool setChargeTargetMillivolts(int mV);   // 4100..4600 (clamp)
  bool setVbusLimitMilliamp(int mA);        // 100..5000 (clamp)
  void enableCharging(bool en);             // no-op stub (lib compat)

  // ---- Rails ---------------------------------------------------------------
  // Backlight: ALDO2
  static constexpr uint16_t kBL_Min_mV = 1800;
  static constexpr uint16_t kBL_Max_mV = 3300;
  bool setBacklightRail(uint16_t millivolt, bool on); // jetzt mit sanfter 2-Step-Rampe

  // LoRa: ALDO4 + DLDO2
  static constexpr uint16_t kLORA_Min_mV = 1800;
  static constexpr uint16_t kLORA_Max_mV = 3300;
  bool setLoRaRails(bool on, uint16_t aldo4_mV = 3300, uint16_t dldo2_mV = 3300);

  // ---- Utils ----------------------------------------------------------------
  static const char* evtName(EventType t);

private:
  // IRQ-Maske konfigurieren
  bool initIrqMask_();

  // ISR/Task
  void  _isr_();
  static void IRAM_ATTR _onIsr_(void* arg);
  static void _evtTaskTrampoline_(void* arg);
  void  _evtTask_();

  // IRQ-Status auslesen/decodieren (mit Button-Sequencing)
  bool  _drainIrqStatus_();

  // Ringbuffer
  static constexpr uint8_t QSIZE = 16;
  Event  _q[QSIZE];
  volatile uint8_t _qh{0};
  volatile uint8_t _qt{0};

  inline void _pushEvt_(EventType t) {
    const uint8_t h = _qh;
    const uint8_t n = (uint8_t)((h + 1U) % QSIZE);
    if (n == _qt) _qt = (uint8_t)((_qt + 1U) % QSIZE); // drop oldest
    _q[h].type  = t;
    _q[h].ts_ms = (uint32_t)millis();
    _qh = n;
  }

  // I2C-Guard (MVP): Mutex für Wire (I2C0)
  bool lockI2C_(TickType_t to_ticks);
  void unlockI2C_();

private:
  XPowersAXP2101 _pmu;
  bool           _ok{false};

  TwoWire*       _bus{nullptr};
  int            _irq_gpio{-1};
  TaskHandle_t   _evtTask{nullptr};
  EventCallback  _cb{nullptr};

  // interner Button-State (Sequencing)
  bool           _btn_down{false};

  // geteilter Mutex für I2C0
  static SemaphoreHandle_t s_i2c0_mtx;
};

// bequeme globale Instanz
extern PMU_AXP2101 PMU;
