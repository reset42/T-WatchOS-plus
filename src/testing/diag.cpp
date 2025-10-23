#include "diag.hpp"
#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <driver/gpio.h>

namespace testing {
namespace diag {

#ifndef DIAG_PIN_AXP_INT
#define DIAG_PIN_AXP_INT 21   // T-Watch S3: AXP2101 INT
#endif

static const char* kDiagLogPath = "/logs/diag.txt";

// ---------- Logging Helpers ----------
static inline void log_line(const String& s) {
  static uint32_t t0 = millis();
  LittleFS.mkdir("/logs");
  if (File f = LittleFS.open(kDiagLogPath, "a")) {
    f.printf("[%8lu] %s\n", (unsigned long)(millis() - t0), s.c_str());
  }
}

static inline void trace(const String& subj, const String& pay="") {
  Serial.print("evt "); Serial.print(subj);
  if (pay.length()) { Serial.print(' '); Serial.print(pay); }
  Serial.println();
}

// ---------- Baustein 2: ISR Ringpuffer ----------
static constexpr int RB_N = 128;
static volatile uint64_t rb_ts[RB_N];
static volatile int rb_w = 0, rb_cnt = 0, rb_over = 0;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int  s_lastLvl = -1;

static void IRAM_ATTR axp_isr(void* /*arg*/) {
  uint64_t t = esp_timer_get_time(); // µs
  int lvl = gpio_get_level((gpio_num_t)DIAG_PIN_AXP_INT);
  s_lastLvl = lvl;
  int w = rb_w;
  rb_ts[w] = t;
  w = (w + 1) % RB_N;
  if (rb_cnt < RB_N) { rb_cnt++; rb_w = w; }
  else { rb_over++; rb_w = w; } // überschreiben (älteste raus)
}

static void compute_isr_stats(String& out, bool clearAfter) {
  // Schnappschuss ohne lange Sperren
  uint64_t snap[RB_N]; int n=0, over=0; int lvl=-1;
  portENTER_CRITICAL(&s_mux);
  n = rb_cnt; over = rb_over; lvl = s_lastLvl;
  if (n > RB_N) n = RB_N;
  int w = rb_w;
  // rekonstruiere chronologisch
  for (int i=0;i<n;i++) {
    int idx = (w - n + i + RB_N) % RB_N;
    snap[i] = rb_ts[idx];
  }
  if (clearAfter) { rb_cnt = 0; rb_over = 0; }
  portEXIT_CRITICAL(&s_mux);

  if (n == 0) { out = "isr: n=0"; return; }

  // Δt in µs
  uint64_t min_dt = UINT64_MAX, max_dt = 0, sum_dt = 0;
  for (int i=1;i<n;i++) {
    uint64_t dt = snap[i] - snap[i-1];
    if (dt < min_dt) min_dt = dt;
    if (dt > max_dt) max_dt = dt;
    sum_dt += dt;
  }
  uint64_t avg_dt = (n>1) ? sum_dt / (n-1) : 0;
  out = String("isr: n=")+n+
        String(" over=")+over+
        String(" lvl=")+(lvl? "high":"low")+
        String(" dt_us[min/avg/max]=")+
        String((unsigned long)min_dt)+"/"+
        String((unsigned long)avg_dt)+"/"+
        String((unsigned long)max_dt);
}

// ---------- Baustein 1: Boot-Pegel dumpen ----------
void dump_boot_levels() {
  int raw = gpio_get_level((gpio_num_t)DIAG_PIN_AXP_INT);
  log_line(String("boot: axp_int=") + (raw?"high":"low"));
  trace("trace.testing.diag.bootlevel", String("axp_int=")+(raw?"high":"low"));
}

// ---------- Baustein 4: gezielter Light-Sleep-Test ----------
void run_light_sleep_until_irq(uint32_t max_ms) {
  log_line(String("sleeptest: enter <= ")+String(max_ms)+ "ms, wake source=axp_int low");
  // Level-low Wake (Light-Sleep): GPIO wakeups aktivieren
  gpio_wakeup_enable((gpio_num_t)DIAG_PIN_AXP_INT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  uint64_t t0 = esp_timer_get_time();
  esp_light_sleep_start(); // USB kann kurz „weg“ sein – wir loggen nur ins FS
  uint64_t t1 = esp_timer_get_time();

  // Nach Wake:
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  String s = String("sleeptest: woke cause=") + String((int)cause) +
             String(" dt_ms=") + String((unsigned)((t1 - t0)/1000));
  log_line(s);
  trace("trace.testing.diag.sleep", s);
}

// ---------- Hintergrundtask: periodisch Stats ausgeben ----------
static void watch_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    String s; compute_isr_stats(s, /*clearAfter*/true);
    log_line(s);
  }
}

void init() {
  LittleFS.mkdir("/logs");
  log_line("===== BOOT =====");
  log_line("diag.init");

  // Pin vorbereiten
  pinMode(DIAG_PIN_AXP_INT, INPUT_PULLUP);
  s_lastLvl = gpio_get_level((gpio_num_t)DIAG_PIN_AXP_INT);

  // ISR scharf
  gpio_set_intr_type((gpio_num_t)DIAG_PIN_AXP_INT, GPIO_INTR_ANYEDGE);
  gpio_install_isr_service(0); // idempotent; „already installed“ ist ok
  gpio_isr_handler_add((gpio_num_t)DIAG_PIN_AXP_INT, axp_isr, nullptr);

  trace("trace.testing.diag.watch",
        String("armed axp_int=")+String(DIAG_PIN_AXP_INT)+
        String(" level=")+(s_lastLvl?"high":"low"));
  log_line(String("watch: armed axp_int=")+String(DIAG_PIN_AXP_INT)+
           String(" level=")+(s_lastLvl?"high":"low"));

  dump_boot_levels(); // Baustein 1

  static bool started=false;
  if (!started) {
    xTaskCreatePinnedToCore(watch_task, "diag_watch", 3072, nullptr, 1, nullptr, 0);
    started = true;
  }
}

// ---------- Baustein 2: On-demand Dump ----------
void dump_isr_stats_now() {
  String s; compute_isr_stats(s, /*clearAfter*/false);
  log_line(String("manual ")+s);
  trace("trace.testing.diag.stats", s);
}

} // namespace diag
} // namespace testing
