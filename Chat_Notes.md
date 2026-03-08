# Chat Notes — BYUI eBadge Firmware Development

A running log of key decisions, hardware findings, and implementation notes from development sessions.

---

## 2026-03-07 — USB OTG / VBUS Hardware Analysis

### Background
Investigated whether the BYUI eBadge V4 hardware supports USB Host mode (e.g., reading a firmware image from a USB flash drive or connecting a keyboard).

### Findings from Netlist (`docs/badge-esp32-s3.net`)

**ESP USB connector (J2): Device-mode only**
- CC1 and CC2 pins (J2) connect only to R5 and R6, both **5.1 kΩ pull-downs to GND**
- 5.1 kΩ on CC is the USB-C spec for UFP (Upstream Facing Port = device). Hardwired — cannot advertise host role.
- VBUS net on J2 (`/VBUSB`): connects only to J2 pins and D8 (ESD protection diode to GND). **Nothing drives it — input-only.**

**5V power rail exists but is not routed to J2 VBUS**
- U2 (TPS630701RNMR) is a buck-boost converter producing `/5V_stable` from the battery
- `/5V_stable` feeds the LED array (AL1–AL24 VDD), J3, J11 — **never routed to J2 VBUS**
- Q3 (HL2301A P-MOSFET): gate on `/USB_5V`, drain on `/VBATT`, source on `Net-(Q3-S)` which connects only to a `Power1` symbol — this is the charger input power path, not a VBUS output switch

**Power switch IC**
- U4 = TP4056 (LiPo charger), VCC/CE both on `/USB_5V` — this is the charge input, not an output path

### Conclusion
USB Host mode requires:
1. VBUS output to power the connected device (5V, up to 500 mA for FS HID)
2. CC pins pulled **up** to 3.3V through 56 kΩ (host) rather than pulled down to GND (device)

V4 has neither. The 5V boost rail exists on the board but is not switched to J2 VBUS, and the CC resistors are fixed pull-downs.

### What J2 CAN do (device mode only)
| Use case | Works? | Notes |
|---|---|---|
| USB serial console (`idf.py monitor`) | ✅ | Built into ESP-IDF |
| USB DFU firmware flash | ✅ | ESP-IDF `usb_dfu` component |
| Badge acts as HID keyboard/mouse | ✅ | TinyUSB HID device |
| Badge acts as USB mass storage | ✅ | TinyUSB MSC |
| Reading a USB keyboard or flash drive | ❌ | Requires host mode — needs VBUS output + CC pull-ups |

### V5 board recommendation
To support USB Host on a future revision:
- Route `/5V_stable` through a P-channel MOSFET high-side switch to J2 VBUS
- Connect the MOSFET gate to a spare GPIO (e.g., GPIO45 or GPIO46)
- Change CC resistors to 56 kΩ pull-ups (or use a USB-C PD controller that supports role switching)

---

## 2026-03-07 — OTA & Update Strategy

### Wireless OTA (implemented)
- `ota_manager` component: downloads firmware binary into PSRAM, verifies SHA-256, writes to inactive OTA partition, reboots
- Manifest JSON format: `{ "version": N, "url": "...", "size": N, "sha256": "..." }`
- NVS namespace `wifi_cfg`, key `mfst` stores the manifest URL (set via portal)

### OTA Site Tooling (implemented)
- `tools/publish.sh` — builds catalog JSON, flash descriptor JSON, copies binaries, rebuilds index.html, git pushes
- `tools/build_index.py` — generates index.html with OTA manifest URLs and full USB flash sections per variant
- `tools/catalog_base.example.json` — template for catalog JSON

### SD Card OTA (not yet implemented — pending decision)
- Hardware: SD card wired on SPI2, GPIO3=CS, GPIO10=MISO, GPIO11=MOSI, GPIO12=CLK (shared with display)
- Plan: mount SD on boot, look for `ebadge_app.bin`, verify SHA-256 sidecar, flash via `esp_ota_ops`, rename to `.done`

### USB serial flash (for instructors)
- `idf.py -p COM<X> flash` from PowerShell (Windows) after running ESP-IDF env setup
- In WSL: `source /home/lynn/esp-idf/export.sh` then flash via WSL only if usbipd is configured
- Bootloader can only be updated via USB — never OTA

---

## 2026-03-07 — Portal Display

### Current layout (portal_mode/portal_mode.c)
- All text at scale 2 (16 px per char, 8 px wide)
- `centre_x()` helper auto-centers strings
- Phase 1 (before device joins): Blue bar header, "Welcome to your" / "ECEN NameBadge!" white on blue, WiFi QR at y=120, "Scan to Join Your" / "Board's WiFi" footer
- Phase 2 (after device joins): Green bar header, "Device Connected!" white on green, URL QR, "Scan to open browser" / "If not open yet." footer
- `display_fill_rect()` added to display component for colored header bars

### Key defines
```c
#define QR_CY     120
#define HDR_Y1    2
#define HDR_Y2    20
#define TEXT_Y1   188
#define TEXT_Y2   206
#define HDR_BAR_H 40   // HDR_Y2 + HDR_SCALE*DISPLAY_FONT_H + 4
```

### Web form
- SSID field pre-filled with `BYUI_Visitor` via `value='BYUI_Visitor'`

---

## 2026-03-07 — Hardware Reference (V4)

### Pinout (confirmed from netlist)
| GPIO | Function | Net name |
|------|----------|----------|
| IO19 | USB D- | `/ESP_USB-` |
| IO20 | USB D+ | `/ESP_USB+` |
| IO3  | SD Card CS | `/GPIO3` |
| IO10 | SPI2 MISO | `/GPIO10` (shared display + SD) |
| IO11 | SPI2 MOSI | `/GPIO11` (shared display + SD) |
| IO12 | SPI2 CLK  | `/GPIO12` (shared display + SD) |

### Power architecture (V4)
| Component | Part | Function |
|-----------|------|----------|
| U2 | TPS630701RNMR | Buck-boost, battery → 5V (`/5V_stable`) for LEDs |
| U4 | TP4056 | LiPo charger, powered from USB-UART VBUS |
| Q3 | HL2301A (P-MOS) | Battery power path switch |
| Q4 | HL2301A (P-MOS) | Secondary power switch |
| U3 | CP2102N | USB-UART bridge (J1 connector) |
| J1 | USB-C 16P | USB-UART port (programming + charging) |
| J2 | USB-C 16P | ESP32-S3 native USB (device mode only) |

---

## ESP-IDF Environment

- IDF path: `/home/lynn/esp-idf/`
- Activate: `source /home/lynn/esp-idf/export.sh`
- Target: `esp32s3`
- Flash: `idf.py -p COM<X> flash` (PowerShell) or `idf.py -p /dev/ttyUSB0 flash` (Linux)
- Build confirmed clean as of 2026-03-07
