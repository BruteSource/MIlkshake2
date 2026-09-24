GreenBoxFlightTracker_merged.bin
=================================

This is a single pre-merged firmware image (bootloader + partition table +
boot_app0 + application, all combined at their correct offsets) — no need to
flash multiple files or know any memory addresses.

To flash with an online flasher (e.g. https://espressif.github.io/esptool-js/
or https://web.esphome.io/):

  Chip:   ESP32-S3
  File:   GreenBoxFlightTracker_merged.bin
  Offset: 0x0

Just select this one file at offset 0x0 and flash — that's it.

Board: Hosyond ESP32-S3 2.8" touchscreen (16MB flash, 8MB octal PSRAM)

WARNING — this wipes saved settings: flashing this single merged file
overwrites the NVS partition (touch calibration, and anything else stored
via Preferences) with blank 0xFF, because the merge process pads the small
gap between the partition table and boot_app0 — which is exactly where NVS
lives — instead of leaving it alone. You'll be prompted to redo the 4-point
touch calibration on the next boot after flashing this way. A normal `pio
run -t upload` (multi-file, targeted-offset flash) does NOT have this
problem and preserves NVS — only single-file merged-bin flashing does.

Rebuilding this file:
  cd "GreenBox Flight Tracker"
  pio run
  cp .pio/build/hosyond-s3/firmware.factory.bin "Flashable Bins/GreenBoxFlightTracker_merged.bin"
