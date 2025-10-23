#include "drv_display_st7789v.hpp"
#include "../core/bus.hpp"
#include <SPI.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace drv::display_st7789v {

// ---------- kleines Telemetry-Wrapper ----------
static inline void EMIT(const char* topic, const String& msg) {
  bus::emit_sticky(String(topic), msg);
}
static inline void EMIT(const char* topic, const char* msg) {
  bus::emit_sticky(String(topic), String(msg ? msg : ""));
}

// -------------------- Pins / SPI --------------------
static constexpr int PIN_SCK   = 18;
static constexpr int PIN_MOSI  = 13;
static constexpr int PIN_CS    = 12;
static constexpr int PIN_DC    = 38;
static constexpr int PIN_BLK   = 45;
// RST unverdrahtet → SWRESET

// SPI: fix (keine Runtime-Umschalter)
static SPIClass spi(FSPI);
static SPISettings spi_cfg(40000000, MSBFIRST, SPI_MODE0); // 40 MHz, MODE0

// -------------------- State --------------------
// Rotation & Color (hart verdrahtet: RGB + invert=on)
static uint8_t  g_rot   = 0;      // 0..3
static bool     g_bgr   = false;  // false → RGB (kein MADCTL_BGR)
static bool     g_invert= true;   // invert = on

// Backlight
static uint8_t  g_pwm_chan = 0;       // LEDC channel
static uint32_t g_pwm_hz   = 20000;   // Ziel-Frequenz
static uint8_t  g_pwm_bits = 10;      // Fallbacks via ledcSetup
static uint8_t  g_min_pct  = 4;       // aus Config
static float    g_gamma    = 2.2f;    // aus Config

// Offsets: dieses Panel nutzt volle 240×240 ohne Shift
static int16_t OFF_X[4] = { 0, 0, 0, 0 };
static int16_t OFF_Y[4] = { 0, 0, 0, 0 };

// ---------------- SPI low level ---------------
static inline void cs_low()  { digitalWrite(PIN_CS, LOW); }
static inline void cs_high() { digitalWrite(PIN_CS, HIGH); }
static inline void dc_cmd()  { digitalWrite(PIN_DC, LOW); }
static inline void dc_data() { digitalWrite(PIN_DC, HIGH); }
static inline void dc_settle(){ delayMicroseconds(1); } // kurze Pause für DC-Settling

// Wichtig: DC **vor** CS setzen (verhindert 1-Bit-Shift → falsche Farben)
static void write_cmd(uint8_t cmd) {
  spi.beginTransaction(spi_cfg);
  dc_cmd(); dc_settle(); cs_low();
  spi.transfer(cmd);
  cs_high(); spi.endTransaction();
}
static void write_data(const uint8_t* d, size_t n) {
  if (!n) return;
  spi.beginTransaction(spi_cfg);
  dc_data(); dc_settle(); cs_low();
  spi.transfer(const_cast<uint8_t*>(d), n);
  cs_high(); spi.endTransaction();
}
static void write_u8(uint8_t v) { write_data(&v, 1); }

// ---------------- Address window --------------
static void set_addr_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x0 = x + OFF_X[g_rot];
  uint16_t y0 = y + OFF_Y[g_rot];
  uint16_t x1 = x0 + w - 1;
  uint16_t y1 = y0 + h - 1;

  uint8_t ca[4] = { uint8_t(x0 >> 8), uint8_t(x0 & 0xFF),
                    uint8_t(x1 >> 8), uint8_t(x1 & 0xFF) };
  uint8_t ra[4] = { uint8_t(y0 >> 8), uint8_t(y0 & 0xFF),
                    uint8_t(y1 >> 8), uint8_t(y1 & 0xFF) };

  write_cmd(CMD_CASET); write_data(ca, 4);
  write_cmd(CMD_RASET); write_data(ra, 4);
  write_cmd(CMD_RAMWR);

  EMIT("trace.drv.display.window",
       String("rot=")+String((int)g_rot)+
       " x0="+String((int)x0)+" y0="+String((int)y0)+
       " w="+String((int)w)+" h="+String((int)h));
}

// ---------------- Backlight -------------------
static void backlight_apply(uint8_t pct) {
  pct = (pct < g_min_pct) ? g_min_pct : pct;
  float p = pct / 100.0f;
  uint32_t max_duty = (1u << g_pwm_bits) - 1u;
  uint32_t duty = (uint32_t)(powf(p, g_gamma) * max_duty + 0.5f);
  ledcWrite(g_pwm_chan, duty);

  EMIT("trace.drv.display.backlight",
       String("pct=") + String(pct) + " duty=" + String(duty));
}
void set_brightness_pct(uint8_t pct) { backlight_apply(pct); }

// ---------------- MADCTL / rotation ----------
static uint8_t madctl_for_rot() {
  uint8_t m = 0x00;
  switch (g_rot & 3) {
    case 0: m = 0x00; break;
    case 1: m = (MADCTL_MX | MADCTL_MV); break;
    case 2: m = (MADCTL_MX | MADCTL_MY); break;
    case 3: m = (MADCTL_MY | MADCTL_MV); break;
  }
  if (g_bgr) m |= MADCTL_BGR; // hart verdrahtet: false → RGB
  return m;
}
static void update_madctl_and_window() {
  uint8_t m = madctl_for_rot();
  write_cmd(CMD_MADCTL); write_u8(m);
  set_addr_window(0, 0, PANEL_W, PANEL_H);
  // Warm-up Write, damit Settings sicher greifen
  uint8_t px[2] = { 0x00, 0x00 };
  write_data(px, 2);
}

// ---------------- Panel init -----------------
static void panel_init() {
  EMIT("trace.drv.display.panel", "init=1 rot=" + String(g_rot));

  write_cmd(CMD_SWRESET); delay(120);
  write_cmd(CMD_SLPOUT);  delay(100);

  // Hart verdrahtete Defaults (keine Runtime-Umschalter):
  // - 16 bpp (RGB565)
  // - Inversion EIN
  // - Color-Order RGB (kein BGR-Bit)
  write_cmd(CMD_COLMOD); write_u8(0x55);
  write_cmd(CMD_INVON);

  update_madctl_and_window(); // nutzt g_bgr=false + OFF_X/Y=0,0
  write_cmd(CMD_DISPON); delay(10);

  EMIT("trace.drv.display.init_defaults",
       "colmod=0x55 invert=on color_order=rgb off_all=0,0");
}

// ---------------- Public API -----------------
void init() {
  // Pins
  pinMode(PIN_CS, OUTPUT);   digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);   digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_BLK, OUTPUT);  digitalWrite(PIN_BLK, LOW);

  // SPI
  spi.end();
  spi.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  EMIT("trace.drv.display.spi", "mode=0 hz=40000000");

  // PWM try → 20k/10bit, Fallback 19.5k/11bit
  bool ok = ledcSetup(g_pwm_chan, g_pwm_hz, g_pwm_bits);
  if (!ok) {
    EMIT("trace.drv.display.pwm",
         "setup_fail hz_req=20000 bits_req=10");
    g_pwm_hz = 19531; g_pwm_bits = 11;
    ok = ledcSetup(g_pwm_chan, g_pwm_hz, g_pwm_bits);
  }
  if (ok) {
    ledcAttachPin(PIN_BLK, g_pwm_chan);
    EMIT("trace.drv.display.pwm",
         String("setup_ok hz=") + String(g_pwm_hz) + " bits=" + String(g_pwm_bits));
  } else {
    digitalWrite(PIN_BLK, HIGH); // Hard ON als letzte Rettung
  }

  panel_init();
  EMIT("trace.drv.display.init", "ok=1");
}

void rotate(uint8_t rot) {
  g_rot = (rot & 3);
  update_madctl_and_window();
  EMIT("trace.drv.display.apply",
       String("key=display.rotate value=") + String(g_rot));
}

// Farblogik ist hart verdrahtet (RGB + invert=on) → Laufzeitänderung ignorieren
void set_color_order_rgb(bool /*rgb_is_true*/) {
  EMIT("trace.drv.display.apply",
       "key=display.color_order ignored=hardwired_rgb");
}

// Vollflächen-Fill (konstant 16bpp, Hi→Lo)
void fill_rgb565(uint16_t rgb565) {
  set_addr_window(0, 0, PANEL_W, PANEL_H);
  spi.beginTransaction(spi_cfg);
  dc_data(); dc_settle(); cs_low();

  uint8_t hi = rgb565 >> 8, lo = rgb565 & 0xFF;
  const uint32_t count = PANEL_W * PANEL_H;
  const uint32_t chunk = 256;
  uint8_t buf[chunk * 2];
  for (uint32_t i = 0; i < chunk; ++i) { buf[2*i] = hi; buf[2*i+1] = lo; }

  uint32_t left = count;
  while (left) {
    uint32_t n = (left > chunk) ? chunk : left;
    spi.transfer(buf, n * 2);
    left -= n;
  }
  cs_high(); spi.endTransaction();

  EMIT("trace.drv.display.fill",
       String("rgb=") + String((rgb565 >> 11) & 0x1F) + "," +
       String((rgb565 >> 5) & 0x3F) + "," + String(rgb565 & 0x1F));
}

void test_pattern(uint8_t /*which*/) {
  const uint16_t C_BG = 0x0000;
  const uint16_t C_FG = 0xFFFF;

  fill_rgb565(C_BG);

  auto hline = [&](int y, uint16_t col){
    set_addr_window(0, y, PANEL_W, 1);
    spi.beginTransaction(spi_cfg);
    dc_data(); dc_settle(); cs_low();
    uint8_t hi = col >> 8, lo = col & 0xFF;
    for (int x=0; x<PANEL_W; ++x){ spi.transfer(hi); spi.transfer(lo); }
    cs_high(); spi.endTransaction();
  };
  auto vline = [&](int x, uint16_t col){
    set_addr_window(x, 0, 1, PANEL_H);
    spi.beginTransaction(spi_cfg);
    dc_data(); dc_settle(); cs_low();
    uint8_t hi = col >> 8, lo = col & 0xFF;
    for (int y=0; y<PANEL_H; ++y){ spi.transfer(hi); spi.transfer(lo); }
    cs_high(); spi.endTransaction();
  };

  // Rahmen
  hline(0, C_FG); hline(PANEL_H-1, C_FG);
  vline(0, C_FG); vline(PANEL_W-1, C_FG);

  // Diagonale ↘
  set_addr_window(0,0,PANEL_W,PANEL_H);
  spi.beginTransaction(spi_cfg);
  dc_data(); dc_settle(); cs_low();
  for (int y=0; y<PANEL_H; ++y){
    for (int x=0; x<PANEL_W; ++x){
      bool on = (x==y) || (x==y-1) || (x==y+1);
      uint16_t c = on ? C_FG : 0x0000;
      spi.transfer(c >> 8); spi.transfer(c & 0xFF);
    }
  }
  cs_high(); spi.endTransaction();

  EMIT("trace.drv.display.apply", "key=display.test");
}

void apply_kv(const String& key, const String& value) {
  // Backlight params
  if (key == "backlight.pwm_timer_hz") {
    uint32_t hz = (uint32_t) value.toInt();
    if (!hz) return;
    g_pwm_hz = hz;
    bool ok = ledcSetup(g_pwm_chan, g_pwm_hz, g_pwm_bits);
    EMIT("trace.drv.display.pwm", ok
      ? String("setup_ok hz=") + String(g_pwm_hz) + " bits=" + String(g_pwm_bits)
      : String("setup_fail hz_req=") + String(g_pwm_hz) + " bits_req=" + String(g_pwm_bits));
    return;
  }
  if (key == "backlight.pwm_resolution_bits") {
    int bits = (int) value.toInt();
    if (bits < 8) bits = 8;
    if (bits > 15) bits = 15;
    g_pwm_bits = (uint8_t) bits;
    bool ok = ledcSetup(g_pwm_chan, g_pwm_hz, g_pwm_bits);
    EMIT("trace.drv.display.pwm", ok
      ? String("setup_ok hz=") + String(g_pwm_hz) + " bits=" + String(g_pwm_bits)
      : String("setup_fail hz_req=") + String(g_pwm_hz) + " bits_req=" + String(g_pwm_bits));
    return;
  }
  if (key == "backlight.min_pct") {
    int v = (int)value.toInt();
    v = std::max(0, std::min(100, v));
    g_min_pct = (uint8_t) v;
    EMIT("trace.drv.display.apply", String("key=")+key+" value="+value);
    return;
  }
  if (key == "backlight.gamma") {
    g_gamma = value.toFloat();
    if (g_gamma < 0.1f) g_gamma = 0.1f;
    EMIT("trace.drv.display.apply", String("key=")+key+" value="+value);
    return;
  }

  // Rotation / Offsets
  if (key == "display.rotate") {
    rotate((uint8_t)value.toInt());
    return;
  }
  if (key == "display.offset.rot0" ||
      key == "display.offset.rot1" ||
      key == "display.offset.rot2" ||
      key == "display.offset.rot3") {
    int comma = value.indexOf(',');
    if (comma > 0) {
      int x = value.substring(0, comma).toInt();
      int y = value.substring(comma + 1).toInt();
      int idx = key.charAt(key.length()-1) - '0';
      if (idx >=0 && idx <=3) { OFF_X[idx] = x; OFF_Y[idx] = y; }
      update_madctl_and_window();
      EMIT("trace.drv.display.apply", String("key=")+key+" value="+value);
    }
    return;
  }

  // Farbschalter sind hart verdrahtet → Laufzeit-Intents ignorieren (nur Telemetrie)
  if (key == "display.color_order" || key == "display.invert") {
    EMIT("trace.drv.display.apply", String("key=")+key+" ignored=hardwired");
    return;
  }

  // Draw commands
  if (key == "display.fill") {
    // RGB888 → RGB565 (inline, keine Helper)
    String rgbHex;
    int vpos = value.indexOf("rgb=");
    if (vpos >= 0) rgbHex = value.substring(vpos+4);
    else {
      vpos = value.indexOf("value=");
      if (vpos >= 0) rgbHex = value.substring(vpos+6);
      else rgbHex = value;
    }
    rgbHex.trim(); rgbHex.toUpperCase();
    uint32_t rgb = (uint32_t) strtoul(rgbHex.c_str(), nullptr, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8)  & 0xFF;
    uint8_t b = (rgb)       & 0xFF;
    uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    fill_rgb565(rgb565);
    return;
  }
  if (key == "display.test") {
    test_pattern(1);
    return;
  }

  // SPI-Profil-Keys (nur Telemetrie übernehmen, keine Funktionseinwirkung)
  if (key == "spi0.slice_ms" || key == "spi0.prio" || key == "spi0.role") {
    EMIT("trace.drv.display.apply", String("key=")+key+" value="+value);
    return;
  }
}

} // namespace drv::display_st7789v
