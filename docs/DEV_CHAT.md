# DEV_CHAT — BYUI eBadge Firmware Development

Combined development log and chat notes from development sessions.

---

# Development Log

Chronological record of design decisions and implementation notes from development sessions.

---

## 2026-03-07

### Build system
- ESP-IDF 5.5 toolchain used; stale CMake cache from a prior incomplete configure was removed (`build/` deleted) and a clean build performed.
- Binary: `build/ebadge_app.bin`, ~1.1 MB, 15% free on the app partition.

---

### Portal — SSID pre-fill
The Wi-Fi SSID input on the captive-portal form (`GET /`) was already pre-filled
with the static default `BYUI_Visitor` via a hardcoded `value=` attribute in the HTML string in `wifi_config/wifi_config.c`.

---

### Portal — detecting when a phone reads the page
Several detection points exist / were discussed:

| Event | How detected |
|-------|-------------|
| Phone joins the AP | `WIFI_EVENT_AP_STACONNECTED` → `s_sta_joined = true` |
| OS captive-portal probe (auto-popup) | `redirect_handler` — 302 redirect to `/` |
| Form page actually loaded | `get_root_handler` — new `s_form_served` flag set here |
| Form submitted | `post_save_handler` |

---

### Portal — Phase 3 display (form loaded)
When `GET /` is served the display now clears and shows plain text instructions instead of the QR code:

```
Fill out the form
on your phone
to continue
```

**Files changed:**
- `wifi_config/wifi_config.c` — added `s_form_served` flag; set in `get_root_handler`; reset in `wifi_config_start()`; exposed via `wifi_config_form_served()`.
- `wifi_config/include/wifi_config.h` — declared `wifi_config_form_served()`; added `<stddef.h>`.
- `portal_mode/portal_mode.c` — added Phase 3 poll branch; calls `display_fill` + three centred `display_print` lines when `wifi_config_form_served()` first returns true.

Portal display phases summary:

| Phase | Trigger | Display content |
|-------|---------|----------------|
| 1 | Portal started | Wi-Fi join QR + "Scan to Join Your Board's WiFi" |
| 2 | Phone joined AP | URL QR + "Device Connected!" + "Scan to open browser" |
| 3 | Form loaded in browser | Plain text: "Fill out the form / on your phone / to continue" |

---

### Persistent storage — form data survives reboots
All five form fields (SSID, password, nickname, email, manifest URL) are written to the dedicated `user_data` NVS partition on submit:

```c
nvs_open_from_partition(WIFI_CONFIG_NVS_PARTITION, WIFI_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
nvs_set_str(h, "ssid", ssid);
nvs_set_str(h, "pass", pass);
nvs_set_str(h, "nick", nick);
nvs_set_str(h, "email", email);
nvs_set_str(h, "mfst", manifest);
nvs_commit(h);
```

The `user_data` partition sits at the top of flash (0x3E0000) and is **never overwritten by OTA**. The OTA manager reads `ssid`/`pass`/`mfst` back from this partition on every boot.

---

### Boot branching — skip portal if already configured
On every boot, `wifi_config_is_configured()` is called before the portal. It opens the `user_data` NVS partition read-only and checks whether a non-empty `nick` key exists. If it does, the portal is skipped entirely.

**New API added to `wifi_config`:**

| Function | Description |
|----------|-------------|
| `wifi_config_is_configured()` | Returns `true` if a nickname is stored in NVS |
| `wifi_config_get_nick(out, outlen)` | Copies the stored nickname into `out` |

**Boot flow:**

```
boot
 └─ wifi_config_is_configured()?
     ├─ YES → show_welcome(nick) → [A+B reset window] → ota_manager_run()
     └─ NO  → portal_mode_run(0) → ota_manager_run()
```

**Files changed:** `main/main.c`, `wifi_config/wifi_config.c`, `wifi_config/include/wifi_config.h`.

---

### Welcome screen — A+B factory reset
After the welcome screen is drawn, a 5-second window opens during which holding **Button A (GPIO 38)** and **Button B (GPIO 18)** simultaneously for 2 continuous seconds triggers a factory reset:

1. Screen shows `"Config erased. Rebooting..."` (red on black).
2. `nvs_flash_erase_partition(WIFI_CONFIG_NVS_PARTITION)` wipes all user data.
3. `esp_restart()` reboots into the portal flow.

If neither button is held the window expires silently and normal operation continues.

A small hint line is shown at the bottom of the welcome screen: **"Press A & B to reconfig"**.

**Files changed:** `main/main.c`.

---

# Chat Notes

Key decisions, hardware findings, and implementation notes from development sessions.

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
