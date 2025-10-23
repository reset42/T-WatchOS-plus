// src/core/bus.hpp
#pragma once
#include <Arduino.h>

namespace bus {

// Textausgabe-Senke (z. B. Serial.println in main)
using sink_fn = void (*)(const String& line);

// Event-Handler für interne Abonnenten (Services)
using evt_handler_t = void (*)(const String& topic, const String& kv);

// Bus initialisieren (setzt globale Ausgabesenke)
void init(sink_fn out);

// Sticky-Emit: speichert (topic -> kv), schreibt "evt <topic> <kv>" an SINK,
// und ruft passende Handler auf.
void emit_sticky(const String& topic, const String& kv);

// Subscribe (Konsolenmodus): nur Sticky-Replay an SINK (keine Handler).
// Rückgabewert: Abo-ID (für unsubscribe)
uint32_t subscribe(const String& pattern);

// Subscribe mit Handler inkl. sofortigem Sticky-Replay
uint32_t subscribe(const String& pattern, evt_handler_t handler);

// Unsubscribe
bool unsubscribe(uint32_t id);
void unsubscribe_all();

} // namespace bus
