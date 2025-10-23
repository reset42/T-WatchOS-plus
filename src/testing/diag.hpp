#pragma once
#include <Arduino.h>

namespace testing {
namespace diag {

void init();

// Manuelle Trigger für gezielte Tests (optional aufrufen in main.cpp):
void dump_boot_levels();                 // Baustein 1
void dump_isr_stats_now();               // Baustein 2
void run_light_sleep_until_irq(uint32_t max_ms); // Baustein 4

} // namespace diag
} // namespace testing
