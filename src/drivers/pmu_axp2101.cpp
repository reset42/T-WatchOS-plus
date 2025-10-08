#include "pmu_axp2101.hpp"

// kleine Helfer
static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// globale Instanz
PMU_AXP2101 PMU;

bool PMU_AXP2101::begin(TwoWire& bus, uint8_t addr, int irq_gpio) {
  if (_ok) return true;

  // WICHTIG: Wire ist bereits initialisiert → (-1,-1), damit XPowers kein begin() triggert
  _ok = _pmu.begin(bus, addr, -1, -1);
  if (!_ok) return false;

  // IRQ-Masken setzen (erst alles aus, dann gewünschte an)
  // XPowers erwartet eine Maske → 0=keine/aus, ~0ULL=alle Bits
  _pmu.disableIRQ(~0ULL);
  initIrqMask_();
  _pmu.clearIrqStatus();

  // GPIO-IRQ
  _irq_gpio = irq_gpio;
  if (_irq_gpio >= 0) {
    pinMode(_irq_gpio, INPUT_PULLUP);               // AXP INT: open-drain, active-low
    attachInterruptArg((gpio_num_t)_irq_gpio, _onIsr_, this, FALLING);
  }

  // Event-Task starten
  xTaskCreatePinnedToCore(_evtTaskTrampoline_, "pmu_evt", 3072, this, 3, &_evtTask, 0);
  return true;
}

void PMU_AXP2101::end() {
  if (!_ok) return;

  if (_irq_gpio >= 0) {
    detachInterrupt((gpio_num_t)_irq_gpio);
  }
  _pmu.disableIRQ(~0ULL);

  if (_evtTask) {
    TaskHandle_t t = _evtTask;
    _evtTask = nullptr;
    vTaskDelete(t);
  }

  // _pmu.end() ist protected → nicht aufrufbar; Instanz lebt statisch weiter.
  _ok = false;
}

void IRAM_ATTR PMU_AXP2101::_onIsr_(void* arg) {
  auto self = static_cast<PMU_AXP2101*>(arg);
  if (!self) return;
  self->_isr_();
}

void PMU_AXP2101::_isr_() {
  BaseType_t woken = pdFALSE;
  if (_evtTask) xTaskNotifyFromISR(_evtTask, 1, eSetValueWithOverwrite, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void PMU_AXP2101::_evtTaskTrampoline_(void* arg) {
  static_cast<PMU_AXP2101*>(arg)->_evtTask_();
}

void PMU_AXP2101::_evtTask_() {
  for (;;) {
    uint32_t v = 0;
    xTaskNotifyWait(0, 0xFFFFFFFF, &v, portMAX_DELAY);
    (void)v;

    if (_drainIrqStatus_() && _cb) {
      Event e;
      while (popEvent(e)) _cb(e);
    }
  }
}

bool PMU_AXP2101::_drainIrqStatus_() {
  uint64_t st = _pmu.getIrqStatus();
  if (!st) return false;
  _pmu.clearIrqStatus();

  if (st & XPOWERS_AXP2101_PKEY_POSITIVE_IRQ) _pushEvt_(EventType::BUTTON_PRESS);
  if (st & XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ) _pushEvt_(EventType::BUTTON_RELEASE);

  if (st & XPOWERS_AXP2101_BAT_CHG_START_IRQ) _pushEvt_(EventType::CHG_START);
  if (st & XPOWERS_AXP2101_BAT_CHG_DONE_IRQ)  _pushEvt_(EventType::CHG_DONE);

  if (st & XPOWERS_AXP2101_VBUS_INSERT_IRQ)   _pushEvt_(EventType::VBUS_IN);
  if (st & XPOWERS_AXP2101_VBUS_REMOVE_IRQ)   _pushEvt_(EventType::VBUS_OUT);

  // Optional: HW Short/Long nur nutzen, wenn später gewünscht
  if (st & XPOWERS_AXP2101_PKEY_SHORT_IRQ)    _pushEvt_(EventType::BUTTON_SHORT);
  if (st & XPOWERS_AXP2101_PKEY_LONG_IRQ)     _pushEvt_(EventType::BUTTON_LONG);

  return true;
}

bool PMU_AXP2101::initIrqMask_() {
  uint64_t mask = 0;
  mask |= (uint64_t)XPOWERS_AXP2101_PKEY_POSITIVE_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_VBUS_INSERT_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_VBUS_REMOVE_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_BAT_CHG_START_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_BAT_CHG_DONE_IRQ;

  _pmu.enableIRQ(mask);
  return true;
}

bool PMU_AXP2101::popEvent(Event &out) {
  if (_qt == _qh) return false;
  const uint8_t t = _qt;
  out = _q[t];
  _qt = (uint8_t)((t + 1U) % QSIZE);
  return true;
}

// ---- Telemetrie -------------------------------------------------------------

PMU_AXP2101::Telemetry PMU_AXP2101::readTelemetry() {
  Telemetry t;
  if (!_ok) return t;

  // Beachte: XPowers 0.2.9 liefert diese Methoden (nicht const):
  t.batt_mV      = _pmu.getBattVoltage();
  t.sys_mV       = _pmu.getSystemVoltage(); // NICHT getSysPowerVoltage()
  t.vbus_mV      = _pmu.getVbusVoltage();
  t.batt_percent = _pmu.getBatteryPercent();
  t.charging     = _pmu.isCharging();
  return t;
}

// ---- Policies / Limits ------------------------------------------------------

bool PMU_AXP2101::setChargeTargetMillivolts(int mV) {
  if (mV < 4100) mV = 4100;
  if (mV > 4600) mV = 4600;
  return _pmu.setChargeTargetVoltage(mV);
}

bool PMU_AXP2101::setVbusLimitMilliamp(int mA) {
  if (mA < 100) mA = 100;
  if (mA > 5000) mA = 5000;
  return _pmu.setVbusCurrentLimit(mA);
}

void PMU_AXP2101::enableCharging(bool en) {
  // Kompatibilität: in dieser Lib-Version verwenden wir keinen direkten Call.
  // Wenn später benötigt: _pmu.setChargingEnable(en); (abhängig von XPowers-Version)
  (void)en;
}

// ---- Rails ------------------------------------------------------------------

bool PMU_AXP2101::setBacklightRail(uint16_t millivolt, bool on) {
  if (!_ok) return false;
  const uint16_t mv = clamp_u16(millivolt, kBL_Min_mV, kBL_Max_mV);
  _pmu.setALDO2Voltage(mv);
  if (on) _pmu.enableALDO2();
  else    _pmu.disableALDO2();
  return true;
}

bool PMU_AXP2101::setLoRaRails(bool on, uint16_t aldo4_mV, uint16_t dldo2_mV) {
  if (!_ok) return false;

  aldo4_mV = clamp_u16(aldo4_mV, kLORA_Min_mV, kLORA_Max_mV);
  dldo2_mV = clamp_u16(dldo2_mV, kLORA_Min_mV, kLORA_Max_mV);

  _pmu.setALDO4Voltage(aldo4_mV);
  _pmu.setDLDO2Voltage(dldo2_mV);

  if (on) {
    _pmu.enableALDO4();
    _pmu.enableDLDO2();
  } else {
    _pmu.disableDLDO2();
    _pmu.disableALDO4();
  }
  return true;
}

const char* PMU_AXP2101::evtName(EventType t) {
  switch (t) {
    case EventType::BUTTON_PRESS:   return "BUTTON_PRESS";
    case EventType::BUTTON_RELEASE: return "BUTTON_RELEASE";
    case EventType::BUTTON_SHORT:   return "BUTTON_SHORT";
    case EventType::BUTTON_LONG:    return "BUTTON_LONG";
    case EventType::CHG_START:      return "CHG_START";
    case EventType::CHG_DONE:       return "CHG_DONE";
    case EventType::VBUS_IN:        return "VBUS_IN";
    case EventType::VBUS_OUT:       return "VBUS_OUT";
    default:                        return "NONE";
  }
}
