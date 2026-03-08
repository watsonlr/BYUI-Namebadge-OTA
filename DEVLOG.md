# Development Log

Chronological record of design decisions and implementation notes from development sessions.

---

## 2026-03-07

### Build system
- ESP-IDF 5.5 toolchain used; stale CMake cache from a prior incomplete configure was removed (`build/` deleted) and a clean build performed.
- Binary: `build/ebadge_app.bin`, ~1.1 MB, 15% free on the app partition.

---

### Portal — SSID pre-fill
The Wi-Fi SSID input on the captive-portal form (`GET /`) was already pre-filled with the static default `BYUI_Visitor` via a hardcoded `value=` attribute in the HTML string in `wifi_config/wifi_config.c`.

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
