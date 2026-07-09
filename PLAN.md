# Badge Launcher — Implementation Plan

Work through the phases in order; each ends with a hardware-verified checkpoint. Flash/console workflow and recovery procedures are in HARDWARE.md — read it before touching a badge.

## Phase 0 — Environment sanity (½ hr)

- `source ~/firmware-lab/.venv/bin/activate` (esptool, pio, meshtastic CLI live there).
- Confirm badge presence: `ls /dev/cu.usbmodem*`, `esptool --port <PORT> --chip esp32s3 flash-id`.
- PlatformIO's IDF penv is already repaired on this Mac (idf-component-manager + requirements.core installed into `~/.platformio/penv/.espidf-5.5.3/`). If a fresh machine: see HARDWARE.md §Build environment.

## Phase 1 — Validate the shared partition table (day 1)

The SPEC's table is the foundation; prove it before building anything on it.

1. New PlatformIO ESP-IDF project (copy `DefconBadge2026/examples/sd-test` as the template; board json in `resources/`). Set the shared table as `partitions.csv`.
2. Rebuild **Doom** against it: doom-badge repo, branch `defcon-badge-doom` — change `board_build.partitions` + sdkconfig partition filename; WAD flash offsets move to 0x330000/0x740000 (update any flash scripts; the code itself finds WADs by partition type, no source change).
3. Rebuild **Meshtastic** against it: `~/firmware-lab/meshtastic-firmware`, branch `retia-power`, env `retia_dcbadge` — point `board_build.partitions` at the shared csv. Verify the app binary ≤ 0x260000.
4. Flash each (full stack: bootloader/PT/app/data at the new offsets), verify each still works standalone. **Checkpoint: both firmwares run on the shared table.** This de-risks everything downstream.
5. Validate open question #2 (384K spiffs): configure Meshtastic (owner, region US — antennas confirmed attached on both bench badges), reboot ×5, settings persist.

## Phase 2 — Minimal launcher app (days 2–3)

Assuming SPEC open-question #1 lands on "minimal custom" (recommended):

1. Skeleton: factory-partition app, USB-Serial/JTAG console, parks the 5 CS pins at boot.
2. Display: lift `spi_lcd.c` from doom-badge (ILI9341 init + full-frame push, pins in sdkconfig already badge-correct there). A simple 8×16 text renderer over a 320×240 RGB565 buffer is plenty — no LVGL.
3. Input: GPIO poll for UP/DOWN/A/B (active-low, copy from doom-badge `i_video.c` I_StartTic).
4. Boot logic (standard-otadata flavor): if UP not held and otadata points at a valid ota_0 → nothing to do, bootloader already booted it (launcher only runs when otadata is invalid or forced). Wire hold-UP → `esp_ota_set_boot_partition(factory)` from the *running app*? No — simpler inversion: otadata normally points at ota_0; entering the launcher = user holds UP **while the app boots**, so each app would need a hook. Avoid that: keep otadata pointed at **factory (launcher)** permanently; launcher checks UP; if not held, `esp_ota_get_next_update_partition`… → actually just `esp_image_verify` + `esp_ota_set_boot_partition(ota_0)` + reboot adds ~700 ms. Prototype both, measure, keep whichever meets the <1 s criterion. (This is SPEC open question #3 — settle it here with data.)
5. SD: mount read-only at menu time (code from sd-test), enumerate `/firmware/*.bin`.
6. Install path: `esp_ota_begin/write/end` streaming from SD with progress bar; then WAD check for Doom (`IWAD` magic + byte size vs files in `/firmware/data/`), `esp_partition_erase_range`+`write` with progress if needed.
7. **Checkpoint: menu on screen, can install a trivial blink app from SD and boot it, and return via hold-UP.**

## Phase 3 — The real payloads (day 4)

1. Produce launcher-format artifacts: `doom-audio.app.bin`, `doom-silent.app.bin`, `meshtastic-standard.app.bin`, `meshtastic-lowpower.app.bin` (plain app images from the Phase-1 builds) + `data/doom1.wad`, `data/prboom-plus.wad` for the SD card.
2. Full acceptance run per SPEC checklist, including the mid-install power-yank test and the Meshtastic-settings-survival test.
3. Two-badge RF regression after a Doom→Meshtastic→Doom→Meshtastic cycle (send_recv harness — HARDWARE.md §Meshtastic testing).

## Phase 4 — Ship (day 5)

1. `launcher.factory.bin` merged image (**`--flash-mode dio`** — QIO header bricks, see HARDWARE.md) that includes launcher + shared PT + the WADs + a default app, so one USB flash sets a badge up completely.
2. Add to the `DefconBadge2026` GitHub repo (RetiaLLC org, gh authed as skickar): `firmware/launcher/` with the factory bin, the SD-card file set, and a README (how to prepare the card, how to add your own firmware). Update the root README and docs/flashing.md.
3. Update the doom-badge repo README with the launcher-build instructions.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Meshtastic outgrows the 2.375M slot in future versions | Headroom is ~350K today; document the constraint; launcher rejects oversized bins gracefully with an on-screen message |
| 384K spiffs too small for Meshtastic | Phase 1 step 5 tests it first; fallback = shrink launcher to 640K and/or pwad slack |
| Interrupted WAD write leaves Doom broken (data partitions aren't rollback-protected) | Launcher re-checks WAD hash before boot; broken → re-copy from SD; worst case USB reflash (never a brick — launcher is factory-resident) |
| Boot-latency criterion missed with chain-load approach | Use standard otadata approach (bootloader boots ota_0 directly); launcher entered only via invalid otadata or a "boot to launcher" marker in NVS that apps could set (optional v2) |
| esptool/monitor traps cost hours | They're all documented with recovery steps in HARDWARE.md — read first |

## Success definition

A beginner with a badge, an SD card, and the DefconBadge2026 repo can: flash `launcher.factory.bin` once over USB, copy the firmware folder to the card, and thereafter switch between DOOM and Meshtastic (and anything else they drop on the card) with three button presses and ~10 seconds — no computer.
