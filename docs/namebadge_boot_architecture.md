# Namebadge Boot Architecture and Firmware Management

## Overview

This document describes the boot architecture, memory layout, OTA update
strategy, and recovery procedures for the Namebadge board based on the
**ESP32‑S3 with external PSRAM**.

Design goals:

-   Persistent loader that cannot be erased by student programs
-   Safe firmware updates via OTA
-   Simple classroom user interaction
-   Robust recovery mechanisms
-   Optional microSD-based full firmware restoration

The badge normally boots **directly into the student application**.\
The loader menu is entered by holding **A + B buttons during reset or
power‑up**.

------------------------------------------------------------------------

# Boot Process

## Stage 1 --- ROM Bootloader

Location: **Inside the ESP32‑S3 silicon**

Responsibilities:

-   Runs immediately after reset
-   Initializes SPI flash interface
-   Loads the second‑stage bootloader from flash

This code is permanent and **cannot be erased or modified**.

------------------------------------------------------------------------

## Stage 2 --- ESP‑IDF Second‑Stage Bootloader

Location:

    Flash address: 0x1000

Responsibilities:

-   Reads the partition table
-   Determines which application partition to boot
-   Loads selected firmware into memory

The second‑stage bootloader is intentionally small and contains **no
networking or UI logic**.

------------------------------------------------------------------------

## Stage 3 --- Factory Loader Application

Location:

    factory partition

Responsibilities:

-   Detect **A+B button press**
-   Launch student applications
-   Display loader menu
-   Configure WiFi
-   Install firmware via OTA
-   Perform system reset / recovery
-   Execute microSD recovery

------------------------------------------------------------------------

# Boot Decision Logic

    Power On
       |
    ROM Bootloader
       |
    Second Stage Bootloader
       |
    Factory Loader App
       |
    Check Buttons (A+B)
       |
       |---- pressed ----> Loader Menu
       |
       |---- not pressed -> Launch Student App

Startup delay before launching the student app should be **very short
(\~100--200 ms)**.

------------------------------------------------------------------------

# Flash Memory Layout

Example for **4 MB flash**:

    Flash Memory Map
    ------------------------------------------------
    0x0000   ROM bootloader (internal)
    0x1000   second-stage bootloader
    0x8000   partition table
    0x9000   NVS (device configuration)
    0xE000   OTA data (boot selection)

    0x10000  factory loader application

    0x110000 ota_0  (student app slot A)
    0x210000 ota_1  (student app slot B)
    ------------------------------------------------

## Partition Roles

  Partition   Purpose
  ----------- -------------------------------------------
  NVS         WiFi credentials and device configuration
  otadata     Tracks active OTA slot
  factory     Loader application
  ota_0       Student application slot
  ota_1       Student application slot

The **factory loader is never overwritten by OTA updates**.

------------------------------------------------------------------------

# Why Two OTA Partitions Exist

OTA updates must **never overwrite the currently running firmware**.

Two slots allow safe updates.

## Example

Current firmware:

    running -> ota_0

New firmware download:

    write new firmware -> ota_1

After reboot:

    running -> ota_1

Next update writes back to:

    ota_0

This alternates forever.

------------------------------------------------------------------------

## What Happens If an Update Fails

If power is lost during download:

    ota_0 remains intact
    ota_1 incomplete

Device can still boot from the previous firmware.

ESP‑IDF also supports **automatic rollback** if the new firmware
crashes.

------------------------------------------------------------------------

# PSRAM Usage

The ESP32‑S3 module includes external PSRAM.

Important:

**PSRAM cannot store boot code.**\
It becomes available only **after the bootloader runs**.

PSRAM is used by applications for:

-   OTA download buffers
-   JSON manifest parsing
-   Display framebuffers
-   Large FreeRTOS stacks

Example framebuffer size:

    320 x 240 x 2 bytes ≈ 150 KB

PSRAM is ideal for this.

------------------------------------------------------------------------

# Loader Menu

When the board boots with **A+B held**, the loader menu appears.

Typical menu:

    1 Install application from OTA
    2 Run installed application
    3 Configure WiFi
    4 Reset to blank canvas
    5 Serial flash mode
    6 microSD recovery

------------------------------------------------------------------------

# Installing Applications via OTA

Steps:

1.  Connect to configured WiFi
2.  Download application manifest
3.  Select application
4.  Download firmware
5.  Write firmware to inactive OTA slot
6.  Reboot into new firmware

ESP‑IDF helper function:

    esp_ota_get_next_update_partition(NULL);

------------------------------------------------------------------------

# Reset to Blank Canvas

Erases student firmware while keeping loader intact.

Steps:

    erase ota_0
    erase ota_1
    erase otadata
    reboot

After reboot the badge returns to the loader.

------------------------------------------------------------------------

# Serial Flash Mode

Allows full firmware flashing using USB or UART.

Enter mode:

    Hold BOOT
    Press RESET
    Release RESET

Flash using:

    esptool.py write_flash

This restores:

-   bootloader
-   partition table
-   factory loader

------------------------------------------------------------------------

# microSD Recovery System

A microSD card can contain a **complete factory firmware bundle**.

When the badge boots with **A+B held and a recovery card inserted**, the
loader restores firmware automatically.

## Recovery Card Layout

    /firmware/
        bootloader.bin
        partition-table.bin
        factory.bin
        ota0.bin
        ota1.bin

or

    /firmware/
        factory_bundle.bin

------------------------------------------------------------------------

## Creating a Firmware Bundle

Use ESP‑IDF and esptool:

    esptool.py merge_bin -o factory_bundle.bin   0x1000 bootloader.bin   0x8000 partition-table.bin   0x10000 factory.bin

Optional OTA apps may also be included.

------------------------------------------------------------------------

## Recovery Procedure

User steps:

1.  Insert recovery microSD card
2.  Hold **A + B**
3.  Press reset
4.  Wait \~10 seconds
5.  Badge automatically restores firmware
6.  Device reboots

------------------------------------------------------------------------

# Recommended Recovery Hierarchy

    1 microSD recovery
    2 OTA reinstall
    3 USB serial flashing
    4 ROM download mode

This ensures the badge can **always be restored**.

------------------------------------------------------------------------

# Suggested Button Behavior

  Buttons      Behavior
  ------------ ------------------------
  none         launch student program
  A+B          open loader menu
  BOOT+RESET   ROM flashing mode

------------------------------------------------------------------------

# Typical Classroom Workflow

Normal use:

    Power on badge
    Student program runs

Install a new assignment:

    Hold A+B
    Reset board
    Loader menu appears
    Install application

Recover a badge:

    Insert microSD recovery card
    Hold A+B
    Reset board
    Badge restores firmware

------------------------------------------------------------------------

# Summary

Key design decisions:

-   Loader stored in **factory partition**
-   Student apps stored in **OTA slots**
-   Device normally boots student program
-   **A+B held during boot opens loader**
-   PSRAM used for buffers and graphics
-   microSD allows fast firmware recovery
-   ROM bootloader guarantees last‑resort flashing

This architecture provides a **robust, classroom‑friendly firmware
system** for the Namebadge project.
