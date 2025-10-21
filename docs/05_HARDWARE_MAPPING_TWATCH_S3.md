# 05 – T‑Watch S3 Hardware Mapping & Constraints (condensed)
- SPI0 Display (ST7789V3): SCK18 MOSI13 CS12 DC38 BL45; start 40 MHz; Y-offset 80 px; warm-up blit after wake.
- SPI1 LoRa (SX1262): SCK3 MOSI1 MISO4 CS5 BUSY7 DIO1=9 RESET=8.
- I2C0: AXP2101(0x34 INT21), BMA423(0x19 INT14), PCF8563(0x51 INT17), DRV2605(0x5A).
- I2C1: FT6336U(0x38 INT16) dedicated; no reset pin → ALDO3 power-cycle for recovery.
- Rails (AXP2101): ALDO2 backlight, ALDO3 display+touch, ALDO4 lora, BLDO2 haptic.
- Soft-Start sequence & brownout guard; wake whitelist as per user.ini.
