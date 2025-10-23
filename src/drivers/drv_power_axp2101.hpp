#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace drv { namespace axp2101 {

// ---------- IRQ Snapshot / Events ----------
struct AxpEvents {
  uint8_t st1{0}, st2{0}, st3{0};
  bool vbus_in{false};
  bool vbus_out{false};
  bool chg_start{false};
  bool chg_done{false};
  bool batt_low{false};
  bool batt_crit{false};
  bool key_short{false};
  bool key_long{false};
};

class Axp2101 {
public:
  Axp2101();
  void   setLog(Stream* s);
  bool   begin(uint32_t i2cHz = 400000, bool release_irq_if_low = true, TwoWire* bus = &Wire);

  // Low-level I2C
  bool   readU8(uint8_t reg, uint8_t& out);
  bool   writeU8(uint8_t reg, uint8_t val);
  bool   readN(uint8_t reg, uint8_t* buf, size_t n);

  // Dumps / Debug
  bool   dumpCore();
  bool   dumpIRQ();
  bool   dumpRails();
  bool   dumpLdoVoltages();

  // IRQ Service
  bool   clearIRQStatus();
  bool   releaseIRQLine();
  bool   waitIntHigh(uint32_t timeout_ms = 10);
  int    intLevel() const;
  bool   handleIRQOnce(bool verbose, AxpEvents* out = nullptr);
  bool   enableIRQMonitor(bool on);
  bool   pollIRQ(bool verbose, AxpEvents* out = nullptr);

  // Masks & Status
  bool   setIRQEnableMask(uint8_t en1, uint8_t en2, uint8_t en3);
  bool   getIRQEnableMask(uint8_t& en1, uint8_t& en2, uint8_t& en3);
  bool   getIRQStatus(uint8_t& st1, uint8_t& st2, uint8_t& st3);

  // ADC Channel Enable (REG 0x30)
  struct AdcCh {
    static constexpr uint16_t ADC_VBAT = 1u << 0;
    static constexpr uint16_t ADC_TS   = 1u << 1;
    static constexpr uint16_t ADC_VBUS = 1u << 2;
    static constexpr uint16_t ADC_VSYS = 1u << 3;
  };
  bool   setAdcEnable(uint16_t mask, bool on);
  bool   setAdcMask(uint16_t mask);
  bool   getAdcMask(uint16_t& out);

  // ADC Reads (14-bit hi6:lo8)
  bool   readVBAT_mV(uint16_t& out);
  bool   readVBUS_mV(uint16_t& out);
  bool   readVSYS_mV(uint16_t& out);
  bool   readICharge_raw(uint16_t& out);
  bool   readIDischarge_raw(uint16_t& out);

  // Input / System Limits
  bool   setInputVoltageLimit_mV(uint16_t mv);
  bool   setInputCurrentLimit_raw(uint8_t code);
  bool   setVsysPowerOffThresh_raw(uint8_t code);

  // Charger
  bool   setPrechargeCurrent_raw(uint8_t code);
  bool   setConstChargeCurrent_raw(uint8_t code);
  bool   setTermCurrent_raw(uint8_t code);
  bool   setChargeVoltage_raw(uint8_t code);
  bool   enableBatteryDetect(bool en);
  bool   enableChargeLED(bool en);
  bool   setRTCBackupChargeVolt_raw(uint8_t code);
  bool   enableRTCBackupCharge(bool en);

  // Rails / LDOs
  bool   setDcdcOnOff(uint8_t mask);
  bool   getDcdcOnOff(uint8_t& out);
  bool   setLdoOnOff0(uint8_t mask);
  bool   setLdoOnOff1(uint8_t mask);
  bool   getLdoOnOff(uint8_t& out0, uint8_t& out1);

  enum LdoReg : uint8_t {
    ALDO1_V = 0x92, ALDO2_V = 0x93, ALDO3_V = 0x94, ALDO4_V = 0x95,
    BLDO1_V = 0x96, BLDO2_V = 0x97, CLDO1_V = 0x98, CLDO2_V = 0x99, CLDO3_V = 0x9A
  };
  bool   setLdoVoltage(LdoReg reg, uint8_t code);
  bool   getLdoVoltage(LdoReg reg, uint8_t& code);

  // T-Watch S3 Default Setup
  bool   twatchS3_basicPowerOn();

  // Sleep/Wake Armierung
  bool   armWakeGpioLow();

private:
  void   logf(const char* fmt, ...) const;

  // Register (Auszug)
  static constexpr uint8_t REG_STATUS1           = 0x00;
  static constexpr uint8_t REG_PWRON_STATUS      = 0x20;
  static constexpr uint8_t REG_PWROFF_STATUS     = 0x21;
  static constexpr uint8_t REG_VSYS_PWROFF_THR   = 0x24;
  static constexpr uint8_t REG_SLEEP_WAKEUP_CTRL = 0x26;
  static constexpr uint8_t REG_IRQ_OFF_ON_LEVEL  = 0x27;
  static constexpr uint8_t REG_ADC_EN            = 0x30;
  static constexpr uint8_t REG_VBAT_H            = 0x34;
  static constexpr uint8_t REG_VBUS_H            = 0x36;
  static constexpr uint8_t REG_VSYS_H            = 0x38;
  static constexpr uint8_t REG_ICHG_H            = 0x3A;
  static constexpr uint8_t REG_IDIS_H            = 0x3C;
  static constexpr uint8_t REG_IRQ_EN1           = 0x40;
  static constexpr uint8_t REG_IRQ_EN2           = 0x41;
  static constexpr uint8_t REG_IRQ_EN3           = 0x42;
  static constexpr uint8_t REG_IRQ_ST1           = 0x48;
  static constexpr uint8_t REG_IRQ_ST2           = 0x49;
  static constexpr uint8_t REG_IRQ_ST3           = 0x4A;
  static constexpr uint8_t REG_PRECHG_I          = 0x61;
  static constexpr uint8_t REG_CC_I              = 0x62;
  static constexpr uint8_t REG_TERM_I            = 0x63;
  static constexpr uint8_t REG_CV_SET            = 0x64;
  static constexpr uint8_t REG_BAT_DET           = 0x68;
  static constexpr uint8_t REG_CHG_LED           = 0x69;
  static constexpr uint8_t REG_RTC_BAK_V         = 0x6A;
  static constexpr uint8_t REG_DCDC_ONOFF        = 0x80;
  static constexpr uint8_t REG_LDO_ONOFF0        = 0x90;
  static constexpr uint8_t REG_LDO_ONOFF1        = 0x91;
  static constexpr uint8_t REG_IIN_VIN_LIMIT     = 0x15;
  static constexpr uint8_t REG_IIN_CUR_LIMIT     = 0x16;
  static constexpr uint8_t SWC_IRQ_PIN_TO_LOW    = (1u << 4);

  // HW Defaults (T-Watch S3)
  TwoWire* _bus;
  uint8_t  _addr;
  int      _intPin;
  int      _sda, _scl;
  Stream*  _log;

  // IRQ-Monitor
  static volatile bool _irqFlag;
  static Axp2101*      _isrOwner;
  static void IRAM_ATTR _isrThunk();
  bool     _irqAttached{false}; // NEW: nur detach, wenn wirklich attached
};

} } // namespace
