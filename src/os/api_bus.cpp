#include "os/api_bus.hpp"

void ApiBus::attach(IApiTransport* t) {
  if (!t) return;
  if (std::find(_transports.begin(), _transports.end(), t) == _transports.end())
    _transports.push_back(t);
}
void ApiBus::detach(IApiTransport* t) {
  _transports.erase(std::remove(_transports.begin(), _transports.end(), t), _transports.end());
}

void ApiBus::registerHandler(const String& ns, ApiHandler h) {
  String n = ns; _toLowerInPlace(n);
  for (auto &e : _handlers) if (e.ns == n) { e.fn = h; return; }
  _handlers.push_back({n, h});
}

IApiTransport* ApiBus::_findTransport(void* opaque) {
  for (auto* t : _transports) if (t == opaque) return t;
  return nullptr;
}

void ApiBus::ingestLine(const String& line, void* origin) {
  ApiRequest r;
  if (!_parseCmd(line, r)) {
    replyErr(origin, "E_BAD_CMD", "bad command syntax");
    return;
  }
  r.origin = origin;

  for (auto &e : _handlers) {
    if (e.ns == r.ns) { e.fn(r); return; }
  }
  replyErr(origin, "E_NO_NS", "unknown namespace");
}

void ApiBus::replyOk(void* to, std::initializer_list<ApiKV> kv) {
  std::vector<ApiKV> v(kv.begin(), kv.end());
  replyOk(to, v);
}
void ApiBus::replyOk(void* to, const std::vector<ApiKV>& kv) {
  IApiTransport* t = _findTransport(to);
  if (!t) return;
  String out = "ok";
  for (auto &p : kv) { out += " " + p.key + "=" + p.val; }
  t->sendLine(out);
}
void ApiBus::replyErr(void* to, const char* code, const char* msg) {
  IApiTransport* t = _findTransport(to);
  if (!t) return;
  String out = "err code="; out += code; out += " msg=\""; out += msg; out += "\"";
  t->sendLine(out);
}

void ApiBus::publishEvent(const String& topic, const std::vector<ApiKV>& kv, void* except) {
  String out = "evt/"; out += topic;
  for (auto &p : kv) out += " " + p.key + "=" + p.val;
  for (auto* t : _transports) if (t != except) t->sendLine(out);
}

const String* ApiBus::findParam(const std::vector<ApiKV>& v, const char* key) {
  for (auto &p : v) if (p.key.equalsIgnoreCase(key)) return &p.val;
  return nullptr;
}

// ---------------- parsing ----------------

void ApiBus::_toLowerInPlace(String& s) {
  for (size_t i=0;i<s.length();++i) s.setCharAt(i, (char)tolower((int)s.charAt(i)));
}

static void _splitFirst(const String& in, char delim, String& head, String& tail) {
  int idx = in.indexOf(delim);
  if (idx < 0) { head = in; tail = ""; return; }
  head = in.substring(0, idx);
  tail = in.substring(idx+1);
}

std::vector<ApiKV> ApiBus::_parseParams(const String& tail) {
  std::vector<ApiKV> out;
  int i = 0, n = tail.length();
  while (i < n) {
    while (i < n && isspace((int)tail[i])) ++i;
    if (i >= n) break;
    int j = i;
    while (j < n && !isspace((int)tail[j])) ++j;
    String tok = tail.substring(i, j);
    int eq = tok.indexOf('=');
    if (eq > 0) {
      ApiKV kv;
      kv.key = tok.substring(0, eq);
      kv.val = tok.substring(eq+1);
      _toLowerInPlace(kv.key);
      out.push_back(std::move(kv));
    }
    i = j + 1;
  }
  return out;
}

bool ApiBus::_parseCmd(const String& line, ApiRequest& out) {
  // expected: "cmd <ns>.<action> k=v k=v"
  String s = line;
  s.trim();
  if (!s.startsWith("cmd ")) return false;
  s.remove(0, 4); // remove "cmd "
  String head, tail;
  _splitFirst(s, ' ', head, tail);

  int dot = head.indexOf('.');
  if (dot <= 0 || dot == (int)head.length()-1) return false;

  out.ns     = head.substring(0, dot);
  out.action = head.substring(dot+1);
  _toLowerInPlace(out.ns);
  _toLowerInPlace(out.action);

  out.params = _parseParams(tail);
  return true;
}
