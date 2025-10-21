# 01 – CORE ARCHITEKTUR & POWER-ZUSTÄNDE (condensed)
- Userspace ist King; Bus-only zwischen Services; Treiber via Interfaces.
- Keine Arbeit im ISR; Scheduler+Arbiter mit Slices (UI Vorrang, Radio aging).
- Power: ready/standby/lightsleep/deepsleep (HW); Last-Call nur vor lightsleep (Drain: config.save, telemetry.flush).
- Backlight-Ramping: service_power orchestriert Rails/Phasen, service_display rampt PWM (GPIO45).
- Sticky States: power.rail_state, power.mode_changed, display.ready, time.ready, config.section, region.*, ui.brightness.
- Config: dev+user ini, Sticky-Prime; Safe-Window ≥ 1500 ms.
- SLOs: Touch→Frame ≤120 ms, Frame ≤50 ms.
