// GPT: Vorbereitender Stub für spätere Implementierung (von Andi gewünscht)
// Schlanker Config-Service:
//  - Lädt /config/dev.ini und /config/user.ini beim Boot
//  - Primed ALLE Key/Value-Paare als Sticky-Events: <section>.<key>  value=<raw>
//  - Kein Merge; dev.ini wird nie geschrieben
//  - user.ini wird nur bei Änderungs-Tracking gespeichert (z.B. ui.brightness)
//  - Bietet minimalen State-Zugriff (ui.brightness) für andere Module

#pragma once
#include <Arduino.h>

namespace config {

// Init liest dev.ini & user.ini und primed die Werte auf den Event-Bus.
//  - dev.ini: read-only, alle Einträge als Sticky 'section.key value=<raw>'
//  - user.ini: dito; bekannte Keys werden in State übernommen
void init();

// Tracking-APIs (von Parser/Modulen aufzurufen)
void note_ui_brightness(int value);

// Status / Save
bool is_dirty();
bool save_now(size_t* bytes_written = nullptr);  // schreibt /config/user.ini
void on_power_last_call();                       // ruft save_now() wenn dirty

// Zugriff auf geladene Werte
bool has_ui_brightness();
int  get_ui_brightness();

// Debug/Snapshot (z. B. "ui.brightness=65")
String snapshot();

} // namespace config
