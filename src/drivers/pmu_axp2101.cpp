#include "drivers/pmu_axp2101.hpp"
#include "drivers/board_pins.hpp"
#include "os/bus_guard.hpp"
#include <Wire.h>

PMU_AXP2101 PMU;

static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool PMU_AXP2101::beginDefault() {
  Wire.begin(TWATCH_S3_I2C0::SDA, TWATCH_S3_I2C0::SCL, TWATCH_S3_I2C0::FREQ_HZ);
  return begin(Wire, 0x34, TWATCH_S3_PMU_Pins::PMU_IRQ);
}

bool PMU_AXP2101::begin(TwoWire& bus, uint8_t addr, int irq_gpio) {
  if (_ok) return true;

  _bus = &bus;

  // WICHTIG: Wire ist bereits initialisiert → (-1,-1), damit XPowers kein begin() triggert
  _ok = _pmu.begin(bus, addr, -1, -1);
  if (!_ok) return false;

  // IRQ-Maske + Clear
  if (!g_bus.lockI2C0(pdMS_TO_TICKS(10))) return false;
  _pmu.disableIRQ(~0ULL);
  _pmu.clearIrqStatus();
  g_bus.unlockI2C0();

  initIrqMask_();

  _irq_gpio = irq_gpio;
  if (_irq_gpio >= 0) {
    pinMode(_irq_gpio, INPUT_PULLUP);
    attachInterruptArg((gpio_num_t)_irq_gpio, _onIsr_, this, FALLING);
  }

  xTaskCreatePinnedToCore(_evtTaskTrampoline_, "pmu_evt", 3072, this, 3, &_evtTask, 0);
  _btn_down = false;
  return true;
}

void PMU_AXP2101::end() {
  if (!_ok) return;

  if (_irq_gpio >= 0) detachInterrupt((gpio_num_t)_irq_gpio);

  if (!g_bus.lockI2C0(pdMS_TO_TICKS(10))) return;
  _pmu.disableIRQ(~0ULL);
  g_bus.unlockI2C0();

  if (_evtTask) { TaskHandle_t t = _evtTask; _evtTask = nullptr; vTaskDelete(t); }

  _btn_down = false;
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
  if (!g_bus.lockI2C0(pdMS_TO_TICKS(5))) return false;
  uint64_t st = _pmu.getIrqStatus();
  _pmu.clearIrqStatus();
  g_bus.unlockI2C0();

  if (!st) return false;

  const bool bit_press   = (st & XPOWERS_AXP2101_PKEY_POSITIVE_IRQ) != 0;
  const bool bit_release = (st & XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ) != 0;
  const bool bit_short   = (st & XPOWERS_AXP2101_PKEY_SHORT_IRQ)    != 0;
  const bool bit_long    = (st & XPOWERS_AXP2101_PKEY_LONG_IRQ)     != 0;

  if (bit_press && !_btn_down) { _pushEvt_(EventType::BUTTON_PRESS); _btn_down = true; }
  if (bit_short) _pushEvt_(EventType::BUTTON_SHORT);
  if (bit_long)  _pushEvt_(EventType::BUTTON_LONG);
  if (bit_release) {
    if (_btn_down) { _pushEvt_(EventType::BUTTON_RELEASE); _btn_down = false; }
  }

  if (st & XPOWERS_AXP2101_BAT_CHG_START_IRQ) _pushEvt_(EventType::CHG_START);
  if (st & XPOWERS_AXP2101_BAT_CHG_DONE_IRQ)  _pushEvt_(EventType::CHG_DONE);
  if (st & XPOWERS_AXP2101_VBUS_INSERT_IRQ)   _pushEvt_(EventType::VBUS_IN);
  if (st & XPOWERS_AXP2101_VBUS_REMOVE_IRQ)   _pushEvt_(EventType::VBUS_OUT);

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
  mask |= (uint64_t)XPOWERS_AXP2101_PKEY_SHORT_IRQ;
  mask |= (uint64_t)XPOWERS_AXP2101_PKEY_LONG_IRQ;

  if (!g_bus.lockI2C0(pdMS_TO_TICKS(5))) return false;
  _pmu.enableIRQ(mask);
  g_bus.unlockI2C0();
  return true;
}

bool PMU_AXP2101::popEvent(Event &out) {
  if (_qt == _qh) return false;
  const uint8_t t = _qt;
  out = _q[t];
  _qt = (uint8_t)((t + 1U) % QSIZE);
  return true;
}

PMU_AXP2101::Telemetry PMU_AXP2101::readTelemetry() {
  Telemetry t;
  if (!_ok) return t;
  if (!g_bus.lockI2C0(pdMS_TO_TICKS(5))) return t;

  t.batt_mV      = _pmu.getBattVoltage();
  t.sys_mV       = _pmu.getSystemVoltage();
  t.vbus_mV      = _pmu.getVbusVoltage();
  t.batt_percent = _pmu.getBatteryPercent();
  t.charging     = _pmu.isCharging();

  g_bus.unlockI2C0();
  return t;
}

bool PMU_AXP2101::setChargeTargetMillivolts(int mV) {
  if (mV < 4100) mV = 4100;
  if (mV > 4600) mV = 4600;
  if (!g_bus.lockI2C0(pdMS_TO_TICKS(5))) return false;
  bool ok = _pmu.setChargeTargetVoltage(mV);
  g_bus.unlockI2C0();
  return ok;
}

bool PMU_AXP2101::setVbusLimitMilliamp(int mA) {
  if (mA < 100) mA = 100;
  if (mA > 5000) mA = 5000;
  if (!g_bus.lockI2C0(pdMS_TO_TICKS(5))) return false;
  bool ok = _pmu.setVbusCurrentLimit(mA);
  g_bus.unlockI2C0();
  return ok;
}

void PMU_AXP2101::enableCharging(bool en) {
  (void)en;
}

// ---- Rails ------------------------------------------------------------------

bool PMU_AXP2101::setBacklightRail(uint16_t millivolt, bool on) {
  if (!_ok) return false;

  const uint16_t target = clamp_u16(millivolt, kBL_Min_mV, kBL_Max_mV);

  if (!g_bus.lockI2C0(pdMS_TO_TICKS(10))) return false;

  if (on) {
    uint16_t pre = (uint16_t)((target * 10U) / 100U);
    if (pre < kBL_Min_mV) pre = kBL_Min_mV;
    if (pre > target)     pre = target;

    _pmu.setALDO2Voltage(pre);
    _pmu.enableALDO2();
    g_bus.unlockI2C0();
    vTaskDelay(pdMS_TO_TICKS(3));
    if (!g_bus.lockI2C0(pdMS_TO_TICKS(10))) return false;
    _pmu.setALDO2Voltage(target);
  } else {
    _pmu.disableALDO2();
  }

  g_bus.unlockI2C0();
  return true;
}

bool PMU_AXP2101::setLoRaRails(bool on, uint16_t aldo4_mV, uint16_t dldo2_mV) {
  if (!_ok) return false;

  aldo4_mV = clamp_u16(aldo4_mV, kLORA_Min_mV, kLORA_Max_mV);
  dldo2_mV = clamp_u16(dldo2_mV, kLORA_Min_mV, kLORA_Max_mV);

  if (!g_bus.lockI2C0(pdMS_TO_TICKS(10))) return false;

  _pmu.setALDO4Voltage(aldo4_mV);
  _pmu.setDLDO2Voltage(dldo2_mV);

  if (on) {
    _pmu.enableALDO4();
    _pmu.enableDLDO2();
  } else {
    _pmu.disableDLDO2();
    _pmu.disableALDO4();
  }

  g_bus.unlockI2C0();
  return true;
}
