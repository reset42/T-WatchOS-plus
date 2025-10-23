// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
// A2: Konsolen-/API-Parser (Spec-Minimum: get|set|do|info|sub|unsub|ping|help)

#pragma once
#include <Arduino.h>

namespace api {

// Antworten gehen über diesen Sink (Serial.println in main)
using sink_fn = void (*)(const String& line);

// Parser initialisieren
void init(sink_fn out);

// Eine komplette Eingabezeile verarbeiten (ohne CR/LF)
void handleLine(const String& line);

} // namespace api
