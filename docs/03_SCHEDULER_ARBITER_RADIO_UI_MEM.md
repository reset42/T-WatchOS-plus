# 03 – SCHEDULER, HW-ARBITER, RADIO/UI & SPEICHER (condensed)
- Scheduler classes: ui, io, radio, sensor, persist, telemetry (aging); power hooks.
- HW-Arbiter: SPI0 display ≤1.5 ms; SPI1 lora ≤0.4 ms; I2C timeouts+reset.
- LoRa one-truth, E_RANGE (no auto fragment), no logs on radios.
- Time service: minute sticky; second only with subs/active UI.
- Display: Dirty-rect + full-blit; assets preload PSRAM; warm-up after wake.
- Flash: bootloader/otadata/app0/app1/spiffs/nvs; PSRAM budgets for assets/logs/radio.
