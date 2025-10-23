#pragma once
#include <Arduino.h>

namespace svc { namespace power {

// Initialisiert Power-Service inkl. PMU (AXP2101), Logging und Bus-Subscriptions.
void init();

} } // namespace svc::power
