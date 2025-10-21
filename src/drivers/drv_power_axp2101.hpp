#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace drv { namespace axp2101 {

// ---------- IRQ Snapshot / Events ----------
struct AxpEvents {
  // rohe Statusbytes (ST1..ST3) — immer gesetzt:
  uint8_t st1{0}, st2{0}, st3{0};

  // nützliche, lose interpretierte Flags:
  bool vbus_in{false};
  bool vbus_out{false};
  bool chg_start{false};
  bool chg_done{false};
  bool batt_low{false};
  bool batt_crit{false};
  bool key_short{false};
  bool key_long{false};
};

// ---------- AXP2101 Treiber ----------
class Axp2101 {
public:
  // Pins defaulten auf T-Watch S3: SDA=10, SCL=11, INT=21
  Axp2101(TwoWire* bus, uint8_t i2c_addr, int intPin,
          int sdaPin = 10, int sclPin = 11)
  : _bus(bus), _addr(i2c_addr), _intPin(intPin),
    _sda(sdaPin), _scl(sclPin), _log(&Serial) {}

  void   setLog(Stream* s) { _log = s ? s : &Serial; }

  // HW-Init & Latches freiräumen
  bool   begin(uint32_t i2cHz = 100000, bool release_irq_if_low = true);

  // ---------- Low-level I2C ----------
  bool   readU8(uint8_t reg, uint8_t& out);
  bool   writeU8(uint8_t reg, uint8_t val);
  bool   readN(uint8_t reg, uint8_t* buf, size_t n);

  // ---------- Dumps / Debug ----------
  bool   dumpCore();                       // 0x20/0x21/0x26/0x27
  bool   dumpIRQ();                        // EN1..3 + ST1..3 + INT
  bool   dumpRails();                      // 0x80, 0x90, 0x91
  bool   dumpLdoVoltages();                // 0x92..0x9A

  // ---------- IRQ Service ----------
  bool   clearIRQStatus();                 // W1C: ST1..ST3
  bool   releaseIRQLine();                 // W1C + clear 0x26.Bit4
  bool   waitIntHigh(uint32_t timeout_ms = 10);
  int    intLevel() const { return digitalRead(_intPin); }
  bool   handleIRQOnce(bool verbose, AxpEvents* out = nullptr);

  // IRQ Enable-Mask (direkter Zugriff)
  bool   setIRQEnableMask(uint8_t en1, uint8_t en2, uint8_t en3);
  bool   getIRQEnableMask(uint8_t& en1, uint8_t& en2, uint8_t& en3);

  // Optionaler GPIO-IRQ-Monitor (attachInterrupt/detachInterrupt)
  bool   enableIRQMonitor(bool on);
  bool   pollIRQ(bool verbose, AxpEvents* out = nullptr); // „drain“ falls Leitung low

  // ---------- ADC Channel Enable (REG 0x30) ----------
  enum AdcCh : uint16_t {
    ADC_VBAT = 1u << 0,    // Battery Voltage
    ADC_TS   = 1u << 1,    // TS pin (disable empfohlen bei Charge)
    ADC_VBUS = 1u << 2,    // VBUS Voltage
    ADC_VSYS = 1u << 3,    // System Voltage
  };
  bool   setAdcEnable(uint16_t mask, bool on);
  bool   setAdcMask(uint16_t mask);        // komplette Maske setzen
  bool   getAdcMask(uint16_t& out);

  // ---------- ADC Reads (14-bit hi6:lo8) ----------
  bool   readVBAT_mV(uint16_t& out);
  bool   readVBUS_mV(uint16_t& out);
  bool   readVSYS_mV(uint16_t& out);
  bool   readICharge_raw(uint16_t& out);
  bool   readIDischarge_raw(uint16_t& out);

  // ---------- Input / System Limits ----------
  bool   setInputVoltageLimit_mV(uint16_t mv);   // REG 0x15 (3.88V..4.76V, 80mV step)
  bool   setInputCurrentLimit_raw(uint8_t code); // REG 0x16
  bool   setVsysPowerOffThresh_raw(uint8_t code);// REG 0x24

  // ---------- Charger ----------
  bool   setPrechargeCurrent_raw(uint8_t code);    // REG 0x61
  bool   setConstChargeCurrent_raw(uint8_t code);  // REG 0x62
  bool   setTermCurrent_raw(uint8_t code);         // REG 0x63
  bool   setChargeVoltage_raw(uint8_t code);       // REG 0x64
  bool   enableBatteryDetect(bool en);             // REG 0x68 bit0
  bool   enableChargeLED(bool en);                 // REG 0x69 bit0
  bool   setRTCBackupChargeVolt_raw(uint8_t code); // REG 0x6A
  bool   enableRTCBackupCharge(bool en);           // REG 0x18 bit2

  // ---------- Rails / LDOs ----------
  bool   setDcdcOnOff(uint8_t mask);  // REG 0x80
  bool   getDcdcOnOff(uint8_t& out);

  bool   setLdoOnOff0(uint8_t mask);  // 0x90
  bool   setLdoOnOff1(uint8_t mask);  // 0x91
  bool   getLdoOnOff(uint8_t& out0, uint8_t& out1);

  // LDO Voltages (0x92..0x9A): V = 0.5V + 0.1V*code
  enum LdoReg : uint8_t {
    ALDO1_V = 0x92, ALDO2_V = 0x93, ALDO3_V = 0x94, ALDO4_V = 0x95,
    BLDO1_V = 0x96, BLDO2_V = 0x97, CLDO1_V = 0x98, CLDO2_V = 0x99, CLDO3_V = 0x9A
  };
  bool   setLdoVoltage(LdoReg reg, uint8_t code);
  bool   getLdoVoltage(LdoReg reg, uint8_t& code);

  // ---------- T-Watch S3 Default Setup ----------
  bool   twatchS3_basicPowerOn();

private:
  void   logf(const char* fmt, ...) const;

  // --------------- Register (Auszug) ---------------
  static constexpr uint8_t REG_STATUS1             = 0x00;

  static constexpr uint8_t REG_PWRON_STATUS        = 0x20;
  static constexpr uint8_t REG_PWROFF_STATUS       = 0x21;
  static constexpr uint8_t REG_VSYS_PWROFF_THR     = 0x24;
  static constexpr uint8_t REG_SLEEP_WAKEUP_CTRL   = 0x26;
  static constexpr uint8_t REG_IRQ_OFF_ON_LEVEL    = 0x27;

  static constexpr uint8_t REG_ADC_EN              = 0x30;

  // ADC result registers (14-bit hi6:lo8)
  static constexpr uint8_t REG_VBAT_H              = 0x34; // 0x34/0x35
  static constexpr uint8_t REG_VBUS_H              = 0x36; // 0x36/0x37
  static constexpr uint8_t REG_VSYS_H              = 0x38; // 0x38/0x39
  static constexpr uint8_t REG_ICHG_H              = 0x3A; // 0x3A/0x3B
  static constexpr uint8_t REG_IDIS_H              = 0x3C; // 0x3C/0x3D

  // IRQ
  static constexpr uint8_t REG_IRQ_EN1             = 0x40;
  static constexpr uint8_t REG_IRQ_EN2             = 0x41;
  static constexpr uint8_t REG_IRQ_EN3             = 0x42;
  static constexpr uint8_t REG_IRQ_ST1             = 0x48;
  static constexpr uint8_t REG_IRQ_ST2             = 0x49;
  static constexpr uint8_t REG_IRQ_ST3             = 0x4A;

  // Charger
  static constexpr uint8_t REG_PRECHG_I            = 0x61;
  static constexpr uint8_t REG_CC_I                = 0x62;
  static constexpr uint8_t REG_TERM_I              = 0x63;
  static constexpr uint8_t REG_CV_SET              = 0x64;

  static constexpr uint8_t REG_BAT_DET             = 0x68; // bit0
  static constexpr uint8_t REG_CHG_LED             = 0x69; // bit0
  static constexpr uint8_t REG_RTC_BAK_V           = 0x6A; // 2.6V + 0.1V*code

  // Rails
  static constexpr uint8_t REG_DCDC_ONOFF          = 0x80;
  static constexpr uint8_t REG_LDO_ONOFF0          = 0x90;
  static constexpr uint8_t REG_LDO_ONOFF1          = 0x91;
  static constexpr uint8_t REG_LDO_V_ALDO1         = 0x92; // ..0x9A

  // Inputs
  static constexpr uint8_t REG_IIN_VIN_LIMIT       = 0x15; // VINDPM
  static constexpr uint8_t REG_IIN_CUR_LIMIT       = 0x16; // IINLIM

  // Misc
  static constexpr uint8_t SWC_IRQ_PIN_TO_LOW      = (1u << 4); // REG 0x26 bit4

  // --------------- HW ---------------
  TwoWire* _bus;
  uint8_t  _addr;
  int      _intPin;
  int      _sda, _scl;
  Stream*  _log;

  // IRQ-Monitor
  static volatile bool _irqFlag;
  static Axp2101*      _isrOwner;
  static void IRAM_ATTR _isrThunk();
};

} } // namespace
