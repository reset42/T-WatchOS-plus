# 02 – UNIVERSAL-API, KONSOLE & KONFIG (condensed)
- API: `<verb> <subject>[.<sub>] [key=value]* [id=<u32>]` + LF; verbs get/set/do/info/sub/unsub/ping/help.
- Serial console modes: api↔log (hotkey), 200 ms log mute after toggle; no logs over radios.
- Config service: get/set config.key ... ; config.save; schema; Sticky-Prime for UI settings (ui.brightness).
- Minimal-boot (1.5 s), then normal_start.
