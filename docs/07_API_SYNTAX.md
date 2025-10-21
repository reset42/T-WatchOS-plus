# 07 – UNIVERSAL API SYNTAX (v0.9 draft, condensed)
- Grammar: one line per command; verbs get|set|do|info|sub|unsub|ping|help.
- Responses: ok/err E_*; Events: evt topic ... (sticky on sub).
- Examples: get time.now; set ui.brightness value=65; do power.standby; sub power.*
- LoRa profile: max_len=80, rps=2, rx_window_ms=200; no logs.
