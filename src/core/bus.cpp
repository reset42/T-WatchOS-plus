// src/core/bus.cpp
#include "bus.hpp"
#include <vector>

namespace bus {

struct Sub {
  uint32_t id;
  String   pattern;
  evt_handler_t handler; // null => Konsolen-Abo (nur SINK-Replay)
};

static sink_fn SINK = nullptr;
static std::vector<Sub> subs;
static std::vector<std::pair<String,String>> stickies;
static uint32_t NEXT_ID = 1;

// sehr einfache Pattern-Match-Logik: '*' als Prefix-/Suffix-Wildcard.
static bool match(const String& pattern, const String& topic) {
  int star = pattern.indexOf('*');
  if (star < 0) return pattern == topic;
  String pre = pattern.substring(0, star);
  String post = pattern.substring(star + 1);
  if (!pre.length() && !post.length()) return true; // "*"
  if (pre.length() && !topic.startsWith(pre)) return false;
  if (post.length() && !topic.endsWith(post)) return false;
  if (pre.length() && post.length()) {
    if (topic.length() < pre.length() + post.length()) return false;
  }
  return true;
}

static void sink_evt(const String& topic, const String& kv) {
  if (!SINK) return;
  String line = "evt " + topic;
  if (kv.length()) { line += " "; line += kv; }
  SINK(line);
}

void init(sink_fn out) {
  SINK = out;
  subs.clear();
  stickies.clear();
  NEXT_ID = 1;
}

void emit_sticky(const String& topic, const String& kv) {
  bool updated = false;
  for (auto &p : stickies) {
    if (p.first == topic) { p.second = kv; updated = true; break; }
  }
  if (!updated) stickies.push_back({topic, kv});
  sink_evt(topic, kv);
  for (const auto &s : subs) {
    if (!s.handler) continue;
    if (match(s.pattern, topic)) {
      s.handler(topic, kv);
    }
  }
}

static uint32_t add_sub(const String& pattern, evt_handler_t h) {
  Sub s;
  s.id = NEXT_ID++;
  s.pattern = pattern;
  s.handler = h;
  subs.push_back(s);
  if (h) {
    for (const auto &p : stickies) {
      if (match(pattern, p.first)) h(p.first, p.second);
    }
  } else {
    for (const auto &p : stickies) {
      if (match(pattern, p.first)) sink_evt(p.first, p.second);
    }
  }
  return s.id;
}

uint32_t subscribe(const String& pattern) {
  return add_sub(pattern, nullptr);
}

uint32_t subscribe(const String& pattern, evt_handler_t handler) {
  return add_sub(pattern, handler);
}

bool unsubscribe(uint32_t id) {
  for (size_t i = 0; i < subs.size(); ++i) {
    if (subs[i].id == id) {
      subs.erase(subs.begin() + i);
      return true;
    }
  }
  return false;
}

void unsubscribe_all() {
  subs.clear();
}

} // namespace bus
