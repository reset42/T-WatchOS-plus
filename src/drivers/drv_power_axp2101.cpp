#include "drv_power_axp2101.hpp"
#include <stdarg.h>
#include "driver/gpio.h"
#include "esp_sleep.h"

namespace drv { namespace axp2101 {

volatile bool Axp2101::_irqFlag = false;
Axp2101*      Axp2101::_isrOwner = nullptr;

static inline uint16_t read14_hi6lo8(uint8_t hi, uint8_t lo) {
  return (uint16_t)((hi & 0x3F) << 8) | lo;
}

Axp2101::Axp2101()
: _bus(&Wire), _addr(0x34), _intPin(21), _sda(10), _scl(11), _log(&Serial) {}

void Axp2101::setLog(Stream* s) { _log = s ? s : &Serial; }

void Axp2101::logf(const char* fmt, ...) const {
  char tmp[192];
  va_list ap; va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  _log->print(tmp);
}

void IRAM_ATTR Axp2101::_isrThunk() {
  if (_isrOwner) _irqFlag = true;
}

bool Axp2101::begin(uint32_t i2cHz, bool release_irq_if_low, TwoWire* bus) {
  if (bus) _bus = bus;

  pinMode(_intPin, INPUT_PULLUP);   // INT: open-drain, active-LOW
  _bus->begin(_sda, _scl);
  _bus->setClock(i2cHz);

  uint8_t v=0;
  if (!readU8(REG_PWRON_STATUS, v)) {
    logf("[AXP] begin FAIL (addr=0x%02x, INT=%d, sda=%d scl=%d, hz=%lu)\n",
         _addr, _intPin, _sda, _scl, (unsigned long)i2cHz);
    return false;
  }
  logf("[AXP] begin OK (addr=0x%02x, INT=%d, sda=%d scl=%d, hz=%lu)\n",
       _addr, _intPin, _sda, _scl, (unsigned long)i2cHz);

  if (release_irq_if_low && intLevel() == LOW) {
    releaseIRQLine();
  }
  return true;
}

// I2C
bool Axp2101::readU8(uint8_t reg, uint8_t& out) {
  _bus->beginTransmission(_addr);
  _bus->write(reg);
  if (_bus->endTransmission(false) != 0) return false;
  int n = _bus->requestFrom((int)_addr, 1);
  if (n != 1) return false;
  out = _bus->read();
  return true;
}
bool Axp2101::writeU8(uint8_t reg, uint8_t val) {
  _bus->beginTransmission(_addr);
  _bus->write(reg);
  _bus->write(val);
  return _bus->endTransmission(true) == 0;
}
bool Axp2101::readN(uint8_t reg, uint8_t* buf, size_t n) {
  _bus->beginTransmission(_addr);
  _bus->write(reg);
  if (_bus->endTransmission(false) != 0) return false;
  int got = _bus->requestFrom((int)_addr, (int)n);
  if (got != (int)n) return false;
  for (int i=0; i<got; ++i) buf[i] = _bus->read();
  return true;
}

// Dumps
bool Axp2101::dumpCore() {
  uint8_t on,off,swc,lvl;
  if (!readU8(REG_PWRON_STATUS,on)) return false;
  if (!readU8(REG_PWROFF_STATUS,off)) return false;
  if (!readU8(REG_SLEEP_WAKEUP_CTRL,swc)) return false;
  if (!readU8(REG_IRQ_OFF_ON_LEVEL,lvl)) return false;
  logf("[AXP] CORE pwr_on20=0x%02x pwr_off21=0x%02x sleep_wakeup26=0x%02x irq_level27=0x%02x\n",
       on,off,swc,lvl);
  return true;
}
bool Axp2101::dumpIRQ() {
  uint8_t e1,e2,e3,s1,s2,s3;
  if (!readU8(REG_IRQ_EN1,e1)) return false;
  if (!readU8(REG_IRQ_EN2,e2)) return false;
  if (!readU8(REG_IRQ_EN3,e3)) return false;
  if (!readU8(REG_IRQ_ST1,s1)) return false;
  if (!readU8(REG_IRQ_ST2,s2)) return false;
  if (!readU8(REG_IRQ_ST3,s3)) return false;
  logf("[AXP] IRQ  en40=0x%02x en41=0x%02x en42=0x%02x st48=0x%02x st49=0x%02x st4A=0x%02x INT=%d\n",
       e1,e2,e3,s1,s2,s3,intLevel());
  return true;
}
bool Axp2101::dumpRails() {
  uint8_t dcdc, l0, l1;
  if (!readU8(0x80, dcdc)) return false;
  if (!readU8(0x90, l0)) return false;
  if (!readU8(0x91, l1)) return false;
  logf("[AXP] RAILS dcdc80=0x%02x ldo90=0x%02x ldo91=0x%02x\n", dcdc, l0, l1);
  return true;
}
bool Axp2101::dumpLdoVoltages() {
  uint8_t v[9];
  if (!readN(0x92, v, sizeof(v))) return false;
  logf("[AXP] LDO_V 92..9A ="); for (int i=0;i<9;++i) logf(" %02x", v[i]); logf("\n");
  return true;
}

// IRQ service
bool Axp2101::clearIRQStatus() {
  bool ok = true;
  ok &= writeU8(REG_IRQ_ST1, 0xFF);
  ok &= writeU8(REG_IRQ_ST2, 0xFF);
  ok &= writeU8(REG_IRQ_ST3, 0xFF);
  return ok;
}
bool Axp2101::releaseIRQLine() {
  bool ok = clearIRQStatus();
  delay(1);
  uint8_t swc = 0;
  if (readU8(REG_SLEEP_WAKEUP_CTRL, swc)) {
    if (swc & SWC_IRQ_PIN_TO_LOW) {
      writeU8(REG_SLEEP_WAKEUP_CTRL, (uint8_t)(swc & ~SWC_IRQ_PIN_TO_LOW));
      delay(1);
    }
  }
  waitIntHigh(10);
  return ok;
}
bool Axp2101::waitIntHigh(uint32_t timeout_ms) {
  uint32_t t0 = millis();
  while ((millis() - t0) <= timeout_ms) {
    if (intLevel() == HIGH) return true;
    delay(1);
  }
  return intLevel() == HIGH;
}
int Axp2101::intLevel() const { return digitalRead(_intPin); }

bool Axp2101::handleIRQOnce(bool verbose, AxpEvents* out) {
  delay(1);
  uint8_t e1=0,e2=0,e3=0,s1=0,s2=0,s3=0, swc=0;
  readU8(REG_IRQ_EN1,e1); readU8(REG_IRQ_EN2,e2); readU8(REG_IRQ_EN3,e3);
  readU8(REG_IRQ_ST1,s1); readU8(REG_IRQ_ST2,s2); readU8(REG_IRQ_ST3,s3);
  readU8(REG_SLEEP_WAKEUP_CTRL, swc);

  if (out) {
    out->st1 = s1; out->st2 = s2; out->st3 = s3;
    out->vbus_in  = (s1 & 0x01);
    out->vbus_out = (s1 & 0x02);
    out->chg_start = (s2 & 0x01);
    out->chg_done  = (s2 & 0x02);
    out->key_short = (s2 & 0x08);
    out->key_long  = (s2 & 0x10);
    out->batt_low  = (s3 & 0x01);
    out->batt_crit = (s3 & 0x02);
  }

  clearIRQStatus();
  if (swc & SWC_IRQ_PIN_TO_LOW) {
    writeU8(REG_SLEEP_WAKEUP_CTRL, (uint8_t)(swc & ~SWC_IRQ_PIN_TO_LOW));
  }
  waitIntHigh(10);

  if (verbose) dumpIRQ();
  return true;
}

bool Axp2101::setIRQEnableMask(uint8_t en1, uint8_t en2, uint8_t en3) {
  bool ok = true;
  ok &= writeU8(REG_IRQ_EN1, en1);
  ok &= writeU8(REG_IRQ_EN2, en2);
  ok &= writeU8(REG_IRQ_EN3, en3);
  return ok;
}
bool Axp2101::getIRQEnableMask(uint8_t& en1, uint8_t& en2, uint8_t& en3) {
  return readU8(REG_IRQ_EN1, en1)
      && readU8(REG_IRQ_EN2, en2)
      && readU8(REG_IRQ_EN3, en3);
}
bool Axp2101::getIRQStatus(uint8_t& st1, uint8_t& st2, uint8_t& st3) {
  return readU8(REG_IRQ_ST1, st1)
      && readU8(REG_IRQ_ST2, st2)
      && readU8(REG_IRQ_ST3, st3);
}

// IRQ monitor with attach/detach guard
bool Axp2101::enableIRQMonitor(bool on) {
  if (!on) {
    if (_irqAttached) {
      detachInterrupt(_intPin);
      _irqAttached = false;
    }
    _irqFlag = false;
    _isrOwner = nullptr;
    return true;
  }

  pinMode(_intPin, INPUT_PULLUP);
  _isrOwner = this;

  if (_irqAttached) {            // nur detach, wenn wir wirklich attached hatten
    detachInterrupt(_intPin);
    _irqAttached = false;
  }

  attachInterrupt(_intPin, _isrThunk, FALLING);
  _irqAttached = true;
  logf("[AXP] irqMon=ON (pin=%d)\n", _intPin);

  if (digitalRead(_intPin) == LOW) {
    logf("[AXP] irqMon: INT low at attach -> draining\n");
    handleIRQOnce(true);
  }
  return true;
}

bool Axp2101::pollIRQ(bool verbose, AxpEvents* out) {
  bool need = false;
  if (_irqFlag)                 need = true;
  if (digitalRead(_intPin) == LOW) need = true;
  if (!need) return false;

  _irqFlag = false;
  handleIRQOnce(verbose, out);
  return true;
}

// ADC enables
bool Axp2101::getAdcMask(uint16_t& out) {
  uint8_t v=0;
  if (!readU8(REG_ADC_EN, v)) return false;
  out = v;
  return true;
}
bool Axp2101::setAdcMask(uint16_t mask) {
  return writeU8(REG_ADC_EN, (uint8_t)(mask & 0xFF));
}
bool Axp2101::setAdcEnable(uint16_t mask, bool on) {
  uint16_t m=0;
  if (!getAdcMask(m)) return false;
  uint16_t nm = on ? (m | mask) : (m & ~mask);
  return setAdcMask(nm);
}

// ADC reads
bool Axp2101::readVBAT_mV(uint16_t& out) {
  uint8_t b[2]; if (!readN(REG_VBAT_H, b, 2)) return false;
  out = read14_hi6lo8(b[0], b[1]); return true;
}
bool Axp2101::readVBUS_mV(uint16_t& out) {
  uint8_t b[2]; if (!readN(REG_VBUS_H, b, 2)) return false;
  out = read14_hi6lo8(b[0], b[1]); return true;
}
bool Axp2101::readVSYS_mV(uint16_t& out) {
  uint8_t b[2]; if (!readN(REG_VSYS_H, b, 2)) return false;
  out = read14_hi6lo8(b[0], b[1]); return true;
}
bool Axp2101::readICharge_raw(uint16_t& out) {
  uint8_t b[2]; if (!readN(REG_ICHG_H, b, 2)) return false;
  out = read14_hi6lo8(b[0], b[1]); return true;
}
bool Axp2101::readIDischarge_raw(uint16_t& out) {
  uint8_t b[2]; if (!readN(REG_IDIS_H, b, 2)) return false;
  out = read14_hi6lo8(b[0], b[1]); return true;
}

// Input/System Limits
bool Axp2101::setInputVoltageLimit_mV(uint16_t mv) {
  int32_t code = ((int32_t)mv - 3880 + 40) / 80;
  if (code < 0) code = 0; if (code > 11) code = 11;
  return writeU8(REG_IIN_VIN_LIMIT, (uint8_t)code);
}
bool Axp2101::setInputCurrentLimit_raw(uint8_t code) {
  return writeU8(REG_IIN_CUR_LIMIT, code);
}
bool Axp2101::setVsysPowerOffThresh_raw(uint8_t code) {
  return writeU8(REG_VSYS_PWROFF_THR, code);
}

// Charger
bool Axp2101::setPrechargeCurrent_raw(uint8_t code) {
  return writeU8(REG_PRECHG_I, code);
}
bool Axp2101::setConstChargeCurrent_raw(uint8_t code) {
  return writeU8(REG_CC_I, code);
}
bool Axp2101::setTermCurrent_raw(uint8_t code) {
  return writeU8(REG_TERM_I, code);
}
bool Axp2101::setChargeVoltage_raw(uint8_t code) {
  return writeU8(REG_CV_SET, code);
}
bool Axp2101::enableBatteryDetect(bool en) {
  uint8_t v=0; if (!readU8(REG_BAT_DET, v)) return false;
  v = en ? (v | 0x01) : (v & ~0x01);
  return writeU8(REG_BAT_DET, v);
}
bool Axp2101::enableChargeLED(bool en) {
  uint8_t v=0; if (!readU8(REG_CHG_LED, v)) return false;
  v = en ? (v | 0x01) : (v & ~0x01);
  return writeU8(REG_CHG_LED, v);
}
bool Axp2101::setRTCBackupChargeVolt_raw(uint8_t code) {
  return writeU8(REG_RTC_BAK_V, code);
}
bool Axp2101::enableRTCBackupCharge(bool en) {
  uint8_t v=0; if (!readU8(0x18, v)) return false;
  v = en ? (v | (1u<<2)) : (v & ~(1u<<2));
  return writeU8(0x18, v);
}

// Rails
bool Axp2101::setDcdcOnOff(uint8_t mask) { return writeU8(0x80, mask); }
bool Axp2101::getDcdcOnOff(uint8_t& out) { return readU8(0x80, out); }
bool Axp2101::setLdoOnOff0(uint8_t mask) { return writeU8(0x90, mask); }
bool Axp2101::setLdoOnOff1(uint8_t mask) { return writeU8(0x91, mask); }
bool Axp2101::getLdoOnOff(uint8_t& out0, uint8_t& out1) {
  return readU8(0x90, out0) && readU8(0x91, out1);
}
bool Axp2101::setLdoVoltage(LdoReg reg, uint8_t code) {
  return writeU8((uint8_t)reg, code);
}
bool Axp2101::getLdoVoltage(LdoReg reg, uint8_t& code) {
  return readU8((uint8_t)reg, code);
}

// Defaults
bool Axp2101::twatchS3_basicPowerOn() {
  bool ok = true;
  ok &= setInputVoltageLimit_mV(4360);
  ok &= setInputCurrentLimit_raw(0x00);
  ok &= setVsysPowerOffThresh_raw(0x00);

  ok &= setLdoVoltage(ALDO1_V, 28);
  ok &= setLdoVoltage(ALDO2_V, 28);
  ok &= setLdoVoltage(ALDO3_V, 28);
  ok &= setLdoVoltage(ALDO4_V, 28);
  ok &= setLdoVoltage(BLDO2_V, 28);

  ok &= setDcdcOnOff(0x01);
  ok &= setLdoOnOff0( (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<5) );
  ok &= setLdoOnOff1(0x00);

  uint16_t adcMask = AdcCh::ADC_VBAT | AdcCh::ADC_VBUS | AdcCh::ADC_VSYS;
  ok &= setAdcMask(adcMask);

  ok &= enableBatteryDetect(true);
  ok &= enableChargeLED(false);
  ok &= setRTCBackupChargeVolt_raw(7);
  ok &= enableRTCBackupCharge(true);

  ok &= setPrechargeCurrent_raw(2);
  ok &= setConstChargeCurrent_raw(4);
  ok &= setTermCurrent_raw(1);
  ok &= setChargeVoltage_raw(4);

  return ok;
}

// Wake armieren
bool Axp2101::armWakeGpioLow() {
  gpio_wakeup_enable((gpio_num_t)_intPin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  return true;
}

} } // namespace
