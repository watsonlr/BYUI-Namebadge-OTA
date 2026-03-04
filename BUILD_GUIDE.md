# Build Guide

## Prerequisites

- ESP-IDF v5.3.1 installed and sourced (see [SETUP_GUIDE.md](SETUP_GUIDE.md))
- Python 3.11+

---

## Build the Starter App

```bash
# Set the target chip (only needed once, result saved in sdkconfig)
idf.py set-target esp32s3

# (Optional) open configuration menu
idf.py menuconfig

# Build
idf.py build
```

Output files are in `build/`:

| File | Description |
|------|-------------|
| `build/bootloader/bootloader.bin` | 2nd-stage bootloader |
| `build/partition_table/partition-table.bin` | Partition table |
| `build/<project>.bin` | Your application binary |

---

## Flash the Badge

```bash
# First-time: erase flash to start clean
idf.py -p /dev/ttyUSB0 erase-flash

# Flash all components (bootloader + partition table + app)
idf.py -p /dev/ttyUSB0 flash

# Flash and open serial monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

On Windows (ESP-IDF PowerShell):

```powershell
idf.py -p COM3 erase-flash
idf.py -p COM3 flash monitor
```

> Press **Ctrl+]** to exit the monitor.

---

## Clean Build

```bash
idf.py fullclean
idf.py build
```

---

## Add Your Own Source Files

1. Add `.c` files to `main/`.
2. List them in `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "my_feature.c"      # ← add here
    INCLUDE_DIRS "."
    REQUIRES
        driver
        nvs_flash
        ...
)
```

3. Add any new ESP-IDF component dependencies to the `REQUIRES` list.

---

## Adding Additional Apps (OTA)

If you want to build separate apps that can be downloaded over-the-air:

1. Create an `Apps/<app_name>/` subdirectory.
2. Add a `CMakeLists.txt` and `main/` inside it (same structure as the root project).
3. Build each app:

```bash
cd Apps/my_app
idf.py set-target esp32s3
idf.py build
```

4. Copy the resulting `.bin` to `ota_files/apps/` and update `ota_files/manifest.json`.
5. Run the OTA server: `python3 simple_ota_server.py` (if added — see `manifest.json.example`).
