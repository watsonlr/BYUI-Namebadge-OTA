# Project Objectives — BYUI Namebadge OTA

## Vision

Build a **permanent, unbrickable bootloader** for the BYUI eBadge V3.0 that lives in the factory partition and acts as a friendly supervisor for the badge. Students never need a laptop or programmer to update their badge — the loader handles everything over Wi-Fi or USB.

The loader presents a simple on-device menu (D-pad navigation, LCD display) and offers three top-level modes:

---

## Objective 1 — Board Configuration Portal

**Goal**: Let a student configure the badge from any phone or laptop, without needing a pre-existing Wi-Fi network.

### How it works
1. User selects **"Configure Device"** from the main menu (or holds a button at boot).
2. The badge starts a **Wi-Fi SoftAP** (SSID: `BYUI_NameBadge`, no password).
3. A **captive portal** web server runs on the badge (HTTP, port 80).
4. The phone auto-opens the portal page (captive portal detection), or the user browses to `http://192.168.4.1`.
5. The portal page (served directly from the badge) provides:
   - **Wi-Fi credentials** — SSID + password for the network the badge should join for OTA
   - **Device nickname** — a friendly name stored in NVS (shown on the badge LCD)
   - **Manifest URL** — the GitHub Pages URL to fetch the app catalog from
   - **Factory reset** — erase all stored settings and return to defaults
6. On form submit, settings are written to NVS and the badge reboots to the main menu.

### Acceptance criteria
- [ ] SoftAP starts within 3 seconds of entering config mode
- [ ] Portal page loads on iOS and Android without manual URL entry (captive portal redirect)
- [ ] All settings persist across power cycles (stored in NVS)
- [ ] Invalid/empty submissions show an error and do not write to NVS
- [ ] Pressing the B button cancels and returns to the main menu without saving

---

## Objective 2 — GitHub Pages App Catalog (OTA Download)

**Goal**: Let a student browse and install any app from a centrally-maintained catalog hosted on GitHub Pages — no instructor laptop required once the badge has Wi-Fi credentials.

### How it works
1. User selects **"Download App"** from the main menu.
2. The badge connects to the stored Wi-Fi network.
3. An HTTPS GET fetches `manifest.json` from the configured GitHub Pages URL
   (default: `https://<org>.github.io/<catalog-repo>/manifest.json`).
4. The badge displays the app list on the LCD (name, version, short description).
5. User navigates with D-pad, selects an app with button A.
6. The binary is downloaded in chunks via `esp_https_ota` and written to the next available OTA slot (`ota_0`, `ota_1`, or `ota_2`).
7. On success, `esp_ota_set_boot_partition()` is called and the badge reboots into the new app.
8. The installed app can call `esp_ota_set_boot_partition(factory)` + `esp_restart()` to return to the loader at any time.

### Manifest format (`manifest.json`)
```json
{
  "version": 1,
  "apps": [
    {
      "name": "Game Launcher",
      "version": "2.1.0",
      "description": "Browse and launch arcade games",
      "url": "https://<org>.github.io/<catalog-repo>/apps/launcher.bin",
      "sha256": "<hex-digest>",
      "size_bytes": 524288
    }
  ]
}
```

### Acceptance criteria
- [ ] Manifest fetched over HTTPS (certificate bundle included in build)
- [ ] SHA-256 of downloaded binary verified before `esp_ota_set_boot_partition()` is called; mismatch aborts and shows error
- [ ] Progress bar shown on LCD during download
- [ ] Download can be cancelled mid-flight with button B (returns to menu, does not change boot partition)
- [ ] If Wi-Fi connection fails, a clear error message is shown with a retry option
- [ ] Installed app version is stored in NVS and shown in the menu ("currently installed: v2.1.0")

---

## Objective 3 — Bare-Metal Canvas Mode

**Goal**: Let a student wipe the current user app and put the badge into a clean state ready to accept completely new code — either from a USB cable or a future OTA push — as if it just came out of the box.

### How it works
1. User selects **"Reset to Canvas"** from the main menu (confirmation prompt required).
2. The loader:
   a. Erases all OTA app partitions (`ota_0`, `ota_1`, `ota_2`).
   b. Clears the `otadata` partition so the next boot falls back to the factory loader.
   c. Optionally erases user NVS keys (app-specific data only; Wi-Fi credentials are preserved unless "Full Reset" is chosen).
3. The badge reboots back into the factory loader with a blank slate.
4. **USB sub-mode (optional)**: if the user holds the Boot button during step 1, the badge reboots directly into **ESP32-S3 ROM download mode** (equivalent to `esptool.py chip_id` target), allowing a connected laptop to flash arbitrary firmware without any button gymnastics.

### Why "Canvas"?
Students in a class may want to start a brand-new experiment from scratch without carrying their laptop. After a Canvas reset, the badge is at exactly the same state as when the instructor handed it out — ready to receive any code via USB or OTA.

### Acceptance criteria
- [ ] Confirmation dialog ("Erase all apps? A=Yes, B=Cancel") prevents accidental wipes
- [ ] After reset, boot sequence correctly selects factory partition (verified via `esp_ota_get_boot_partition()`)
- [ ] "Canvas + USB" sub-mode restarts into ROM download mode (GPIO0 held LOW before `esp_restart()`)
- [ ] LCD shows clear status messages during erase ("Erasing app partitions… Done")
- [ ] Operation completes in under 5 seconds

---

## Main Menu Layout (reference)

```
┌─────────────────────────────┐
│  BYUI eBadge  v1.0          │
│  [nickname]                 │
├─────────────────────────────┤
│  ▶ 1. Download App          │
│    2. Configure Device      │
│    3. Reset to Canvas       │
├─────────────────────────────┤
│  Installed: Game Launcher   │
│             v2.1.0          │
└─────────────────────────────┘
  U/D = navigate   A = select
```

---

## Non-Goals (out of scope for v1)

- No peer-to-peer or Bluetooth app transfer
- No app signing beyond SHA-256 checksum
- No multi-user NVS profiles
- No OTA update of the loader itself (factory partition is intentionally permanent)

---

## Milestones

| # | Milestone | Objectives |
|---|-----------|------------|
| M1 | Hardware bringup | LCD on, buttons read, RGB LED, boot into loader skeleton |
| M2 | Configuration portal | Obj 1 complete |
| M3 | OTA catalog | Obj 2 complete |
| M4 | Canvas mode | Obj 3 complete |
| M5 | Integration + polish | Full menu, error handling, SHA-256, progress UI |
| M6 | Catalog repo | GitHub Pages site with initial app set |
