// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
#include "service_touch.hpp"
#include "../drivers/drv_touch_ft6236u.hpp"
#include "../core/bus.hpp"

namespace svc { namespace touch {

static bool s_wake_touch_standby    = true;  // default wie user.ini
static bool s_wake_touch_lightsleep = false; // default wie user.ini
static bool s_irq_on                = true;  // aktueller IRQ-Zustand
static bool s_active                = true;  // aktueller Power-Zustand (active/sleep)

static String kv_find(const String& args, const char* key) {
  String needle = String(key) + "=";
  int i = args.indexOf(needle);
  if (i < 0) return "";
  int start = i + needle.length();
  int end = args.indexOf(' ', start);
  if (end < 0) end = args.length();
  return args.substring(start, end);
}

static inline void TRACE(const char* topic, const String& msg){
  bus::emit_sticky(String(topic), msg);
}

static void apply_power(bool active){
  s_active = active;
  drv::touch_ft6236u::apply_kv("touch.power", active ? "active" : "sleep");
}

static void apply_irq(bool on){
  s_irq_on = on;
  drv::touch_ft6236u::apply_kv("touch.irq", on ? "on" : "off");
}

static void enter_standby(){
  apply_power(false);
  apply_irq(s_wake_touch_standby); // on = Wake-Quelle erlaubt
  TRACE("trace.svc.touch.state",
        String("state=standby power=")+(s_active?"active":"sleep")+
        " irq=" + (s_irq_on?"on":"off"));
}

static void enter_lightsleep(){
  apply_power(false);
  apply_irq(s_wake_touch_lightsleep); // oft off
  TRACE("trace.svc.touch.state",
        String("state=lightsleep power=")+(s_active?"active":"sleep")+
        " irq=" + (s_irq_on?"on":"off"));
}

static void enter_ready(){
  apply_power(true);
  apply_irq(true);
  TRACE("trace.svc.touch.state",
        String("state=ready power=")+(s_active?"active":"sleep")+
        " irq=" + (s_irq_on?"on":"off"));
}

static void on_power_evt(const String& topic, const String& kv){
  if (topic == "power.intent") {
    String tgt = kv_find(kv, "target"); tgt.toLowerCase();
    if (tgt == "standby")    { enter_standby();    return; }
    if (tgt == "lightsleep") { enter_lightsleep(); return; }
    if (tgt == "ready")      { enter_ready();      return; }
  }
}

static void on_wake_policy(const String& topic, const String& kv){
  bool on = (kv.indexOf("on")>=0) || (kv.indexOf("true")>=0) || (kv.indexOf("1")>=0);
  if (topic == "wake.touch_standby") {
    s_wake_touch_standby = on;
    TRACE("trace.svc.touch.policy", String("touch_standby=")+(on?1:0));
    return;
  }
  if (topic == "wake.touch_lightsleep") {
    s_wake_touch_lightsleep = on;
    TRACE("trace.svc.touch.policy", String("touch_lightsleep=")+(on?1:0));
    return;
  }
}

void init(){
  // Treiber init (I2C/IRQ Setup), falls das nicht schon woanders geschieht.
  drv::touch_ft6236u::init();

  // Power-Intents steuern Touch-Power/IRQ
  bus::subscribe("power.intent", on_power_evt);

  // Wake-Policy aus dev.ini/user.ini (wird beim Boot als Sticky geprimed)
  bus::subscribe("wake.touch_standby", on_wake_policy);
  bus::subscribe("wake.touch_lightsleep", on_wake_policy);

  // I2C-Härtung fürs Touch (Forward) – KORREKT auf I²C1 mappen.
  bus::subscribe("i2c1.*", [](const String& topic, const String& value){
    drv::touch_ft6236u::apply_kv(topic, value);
  });

  // Bei Ready standardmäßig aktiv + IRQ an
  enter_ready();
}

} } // namespace svc::touch
