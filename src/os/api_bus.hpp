#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include <algorithm>

struct ApiKV { String key; String val; };

struct ApiRequest {
  String ns;          // namespace  (z.B. "power")
  String action;      // action     (z.B. "mode")
  std::vector<ApiKV> params;
  void*   origin{nullptr}; // Transport-Absender (opaque pointer)
};

class IApiTransport {
public:
  virtual ~IApiTransport() = default;
  virtual const char* name() const = 0;
  virtual bool sendLine(const String& line) = 0;
};

using ApiHandler = std::function<void(const ApiRequest& req)>;

class ApiBus {
public:
  void attach(IApiTransport* t);
  void detach(IApiTransport* t);

  void registerHandler(const String& ns, ApiHandler h);

  // Eingehende Zeile (z. B. "cmd power.mode mode=ready")
  void ingestLine(const String& line, void* origin);

  // Helfer zum Antworten
  void replyOk(void* to, std::initializer_list<ApiKV> kv = {});
  void replyOk(void* to, const std::vector<ApiKV>& kv);
  void replyErr(void* to, const char* code, const char* msg);

  // Events an alle (optional: Absender ausschließen)
  void publishEvent(const String& topic, const std::vector<ApiKV>& kv, void* except=nullptr);

  // Util
  static const String* findParam(const std::vector<ApiKV>& v, const char* key);

private:
  struct HandlerEntry { String ns; ApiHandler fn; };
  std::vector<IApiTransport*> _transports;
  std::vector<HandlerEntry>   _handlers;

  IApiTransport* _findTransport(void* opaque);
  static void _toLowerInPlace(String& s);
  static bool _parseCmd(const String& line, ApiRequest& out);
  static std::vector<ApiKV> _parseParams(const String& tail);
};
