// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
// A2: Universal-API Parser – mit Set-Unterstützung für power.*, i2c0.*, backlight.*, ui.brightness

#include "api_parser.hpp"
#include "bus.hpp"
#include "../services/service_config.hpp"

#include <stdlib.h>
#include <vector>
#include <algorithm>

namespace api {

// ---------------------------------------------------------------------------
// Output helpers
static sink_fn OUT = nullptr;
static inline void ok(const String& s)  { if (OUT) OUT(s); }
static inline void errc(const char* code, const String& msg) {
  if (OUT) OUT(String("err code=") + code + " msg=\"" + msg + "\"");
}

// ---------------------------------------------------------------------------
// KV helpers
static String kv_find(const String& args, const char* key) {
  String needle = String(key) + "=";
  int i = args.indexOf(needle);
  if (i < 0) return "";
  int start = i + needle.length();
  int end = args.indexOf(' ', start);
  if (end < 0) end = args.length();
  return args.substring(start, end);
}

// ---------------------------------------------------------------------------
// Internal state / caches
static unsigned s_epoch_now = 0;

// Track IDs der **Konsolen-Subscriptions** (ohne Handler) → sicheres "unsub *"
static std::vector<uint32_t> s_console_sub_ids;

// Sticky-Cache für ui.brightness
static int s_ui_brightness_cached = -1;

// Emit-Guard: nur wirklich gefährliche Topics sperren (Owner/State/Interna)
static bool is_forbidden_emit(const String& topic) {
  if (topic.startsWith("trace.") || topic.startsWith("drv.")) return true;
  if (topic.startsWith("sys.") || topic.startsWith("pmu."))   return true;
  if (topic == "power.mode_changed" || topic == "power.last_call") return true;
  return false;
}

// Set-Whitelist (zusätzlich zu ui.brightness)
static bool is_allowed_set_topic(const String& topic) {
  // harte Sperren zuerst
  if (topic == "power.intent" || topic == "power.mode_changed" || topic == "power.last_call")
    return false;
  if (topic.startsWith("trace.") || topic.startsWith("drv.") || topic.startsWith("sys.") || topic.startsWith("pmu."))
    return false;

  if (topic == "ui.brightness") return true; // Spezialfall (Owner: UI/Display)

  // Whitelist-Präfixe
  if (topic.startsWith("power."))     return true; // Policy, Ramp, Brownout, etc. (kein intent!)
  if (topic.startsWith("i2c0."))      return true; // timeout_ms, retry
  if (topic.startsWith("backlight.")) return true; // pwm_timer_hz, pwm_resolution_bits, gamma, min_pct
  return false;
}

// ---------------------------------------------------------------------------
// Lifecycle
void init(sink_fn out) {
  OUT = out;

  // Internes Abo zum Cachen aktueller ui.brightness-Stickies
  bus::subscribe("ui.brightness",
    [](const String& /*topic*/, const String& kv) {
      String v = kv_find(kv, "value");
      if (v.length()) s_ui_brightness_cached = atoi(v.c_str());
    }
  );
}

// ---------------------------------------------------------------------------
// Commands
static void cmd_sub(const String& args) {
  String p = args; p.trim();
  if (p.length() == 0) { errc("E_SYNTAX", "bad command syntax"); return; }
  uint32_t id = bus::subscribe(p);                 // console subscription (no handler)
  s_console_sub_ids.push_back(id);                 // nur diese gehören dem Parser
  ok("ok sub id=" + String(id) + " pattern=" + p);
}

static void cmd_unsub(const String& args) {
  String a = args; a.trim();

  // Sicheres "unsub *": ausschließlich Parser-eigene Console-Subs entfernen
  if (a == "*") {
    unsigned cnt = 0;
    for (uint32_t id : s_console_sub_ids) {
      if (bus::unsubscribe(id)) ++cnt;
    }
    s_console_sub_ids.clear();
    ok("ok unsub console_all count=" + String(cnt));
    return;
  }

  // Einzelnes ID-Unsubscribe
  char* endp = nullptr;
  uint32_t id = (uint32_t) strtoul(a.c_str(), &endp, 10);
  if (endp == a.c_str()) { errc("E_SYNTAX", "bad command syntax"); return; }

  bool res = bus::unsubscribe(id);
  if (res) {
    auto it = std::find(s_console_sub_ids.begin(), s_console_sub_ids.end(), id);
    if (it != s_console_sub_ids.end()) s_console_sub_ids.erase(it);
    ok("ok unsub id=" + String(id));
  } else {
    errc("E_UNKNOWN", "unknown subscription id");
  }
}

static void cmd_emit(const String& args) {
  int sp = args.indexOf(' ');
  if (sp <= 0) { errc("E_SYNTAX", "bad command syntax"); return; }

  String topic = args.substring(0, sp); topic.trim();
  String kv    = args.substring(sp + 1); kv.trim();
  if (!topic.length() || !kv.length()) { errc("E_SYNTAX", "bad command syntax"); return; }

  if (is_forbidden_emit(topic)) {
    errc("E_FORBIDDEN", "emit to protected topic");
    return;
  }

  bus::emit_sticky(topic, kv);
  ok("ok emit " + topic);
}

// ---------------------------------------------------------------------------
// Verbs
static void do_get(const String& subj_in, const String& /*args*/) {
  String subj = subj_in; subj.trim();

  if (subj == "ui.brightness") {
    int v = (s_ui_brightness_cached >= 0)
              ? s_ui_brightness_cached
              : (config::has_ui_brightness() ? config::get_ui_brightness() : 50);
    ok("ok ui.brightness value=" + String(v));
    return;
  }

  if (subj == "time.now") {
    ok("ok time.now epoch=" + String(s_epoch_now));
    return;
  }

  errc("E_UNKNOWN", "unknown subject");
}

static void do_set(const String& subj_in, const String& args_in) {
  String subj = subj_in; subj.trim();
  String args = args_in; args.trim();

  // ui.brightness (Sonderfall + Persistenz-Note)
  if (subj == "ui.brightness") {
    String v = kv_find(args, "value");
    if (!v.length()) { errc("E_SYNTAX", "missing value"); return; }
    int val = atoi(v.c_str()); if (val < 0) val = 0; if (val > 100) val = 100;

    bus::emit_sticky("ui.brightness", "value=" + String(val));
    s_ui_brightness_cached = val;
    config::note_ui_brightness(val);

    ok("ok set ui.brightness value=" + String(val));
    return;
  }

  // Whitelisted Topics: power.*, i2c0.*, backlight.*
  if (is_allowed_set_topic(subj)) {
    if (is_forbidden_emit(subj)) { errc("E_FORBIDDEN", "set to protected topic"); return; }

    String v = kv_find(args, "value");
    if (!v.length()) { errc("E_SYNTAX", "missing value"); return; }

    bus::emit_sticky(subj, "value=" + v);
    ok("ok set " + subj + " value=" + v);
    return;
  }

  errc("E_UNKNOWN", "unknown subject");
}

static void do_do(const String& subj_in, const String& args_in) {
  String subj = subj_in; subj.trim();
  String args = args_in; args.trim();

  // Power: Parser erzeugt **nur Intents**, Owner ist der PowerService
  if (subj == "power.ready")     { bus::emit_sticky("power.intent", "target=ready origin=api");      ok("ok do power.ready");      return; }
  if (subj == "power.standby")   { bus::emit_sticky("power.intent", "target=standby origin=api");    ok("ok do power.standby");    return; }
  if (subj == "power.lightsleep"){ bus::emit_sticky("power.intent", "target=lightsleep origin=api"); ok("ok do power.lightsleep"); return; }
  if (subj == "power.deepsleep") { bus::emit_sticky("power.intent", "target=deepsleep origin=api");  ok("ok do power.deepsleep");  return; }

  // (Optional) kleine Cal-Hooks bleiben, Ownership beim Display-Service
  if (subj == "display.cal") {
    String op    = kv_find(args, "op");
    String rot   = kv_find(args, "rot");
    String gamma = kv_find(args, "gamma");

    if (args.startsWith("start") || op == "start") { bus::emit_sticky("ui.cal.cmd", "op=start"); ok("ok do display.cal start"); return; }
    if (args.startsWith("stop")  || op == "stop")  { bus::emit_sticky("ui.cal.cmd", "op=stop");  ok("ok do display.cal stop");  return; }
    if (args.startsWith("next")  || op == "next")  { bus::emit_sticky("ui.cal.cmd", "op=next");  ok("ok do display.cal next");  return; }

    if (rot.length()) {
      int r = atoi(rot.c_str()); if (r < 0) r = 0; if (r > 3) r = 3;
      bus::emit_sticky("ui.cal.rot", "value=" + String(r));
      ok("ok do display.cal rot=" + String(r));
      return;
    }

    if (gamma.length()) {
      bus::emit_sticky("ui.cal.gamma", "value=" + gamma);
      ok("ok do display.cal gamma=" + gamma);
      return;
    }

    errc("E_SYNTAX", "usage: do display.cal start|stop|next|rot=<0..3>|gamma=<f>");
    return;
  }

  // Config: delegiert an ConfigService (Owner)
  if (subj == "config.save") {
    size_t bw = 0; bool okw = config::save_now(&bw);
    ok(String(okw ? "ok" : "err") + " config.save wrote=" + String((unsigned)bw));
    return;
  }

  errc("E_UNKNOWN", "unknown subject");
}

static void do_info(const String& subj_in, const String& /*args_in*/) {
  String subj = subj_in; subj.trim();

  if (subj == "heap" || subj == "sys.heap") {
    ok("ok heap free=" + String((unsigned)ESP.getFreeHeap()));
    return;
  }

  if (subj == "config") {
    String snap = config::snapshot();
    ok("ok config dirty=" + String(config::is_dirty() ? "true" : "false")
       + (snap.length() ? " keys=" + snap : ""));
    return;
  }

  errc("E_UNKNOWN", "unknown subject");
}

// ---------------------------------------------------------------------------
// Router
void handleLine(const String& raw) {
  String line = raw; line.trim();
  if (!line.length()) return;

  if (line == "ping") { ok("ok pong"); return; }
  if (line == "help") { ok("ok cmds=ping,heap,sub,unsub,emit,get,set,do,info"); return; }
  if (line == "heap") { ok("ok heap free=" + String((unsigned)ESP.getFreeHeap())); return; }

  int sp = line.indexOf(' ');
  String verb = (sp < 0) ? line : line.substring(0, sp);
  String rest = (sp < 0) ? ""   : line.substring(sp + 1);

  String subj, args;
  if (rest.length()) {
    int sp2 = rest.indexOf(' ');
    subj = (sp2 < 0) ? rest : rest.substring(0, sp2);
    args = (sp2 < 0) ? ""   : rest.substring(sp2 + 1);
  }

  if (verb == "sub")   { cmd_sub(rest);   return; }
  if (verb == "unsub") { cmd_unsub(rest); return; }
  if (verb == "emit")  { cmd_emit(rest);  return; } // Intent-Emit (mit Guard)

  if (verb == "get")  { do_get(subj, args);  return; }
  if (verb == "set")  { do_set(subj, args);  return; }
  if (verb == "do")   { do_do(subj, args);   return; }
  if (verb == "info") { do_info(subj, args); return; }

  errc("E_UNKNOWN", "unknown verb");
}

} // namespace api
