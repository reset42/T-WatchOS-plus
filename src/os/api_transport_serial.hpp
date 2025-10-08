#pragma once
#include "os/api_bus.hpp"

class SerialTransport : public IApiTransport {
public:
  const char* name() const override { return "serial"; }
  bool sendLine(const String& line) override {
    Serial.println(line);
    return true;
  }
};