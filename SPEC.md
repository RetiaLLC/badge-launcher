# Badge Launcher — Project Spec

## Goal

A resident **firmware launcher** for the Retia 2024 DEF CON badge (ESP32-S3): on boot (or on a held button), show a menu on the TFT, let the user pick a firmware — installed in flash or as a `.bin` on the micro-SD card — and boot it. Think dual-boot for the badge, no computer required.

Primary firmwares to support from day one: **DOOM** and **Meshtastic** (both already ported/built for this badge — see resources). Secondary: any app-format `.bin` a user drops on the SD card.

## Background you must internalize first

- ESP32 **cannot execute from SD**. The launcher copies the selected image into an internal-flash partition (ESP-IDF OTA APIs), sets the boot partition, reboots. "Switching" = fast reflash-from-SD, not true multiboot.
- Prior art (cloned in `upstream/Launcher`): [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) (feature-rich: SD install, web UI, multi-app menu) and [tobozo/M5Stack-SD-Updater](https://github.com/tobozo/M5Stack-SD-Updater) (the classic menu-in-factory-partition pattern). Read both for patterns; neither targets this badge out of the box.
- Read `resources/docs/pinout.md` **fully** before writing code — this badge has crossed touch/module-SD pins, a shared 5-CS SPI bus, a native-USB download-mode trap, and a QIO-header brick trap. All previously hit and documented.

## The flash budget (the hard constraint, already worked out)

8 MB flash total. Everything below fits **fully resident** — Doom's WADs, Meshtastic's settings, and the launcher coexist, so switching only rewrites the ~1–2 MB app slot (≈10 s):

| Partition | Type | Offset | Size | Notes |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x5000 | Same offset/size as Meshtastic's table (compat) |
| otadata | data/ota | 0xE000 | 0x2000 | Same as Meshtastic |
| launcher | app/factory | 0x10000 | 0xC0000 (768K) | **Launcher lives here, never overwritten** |
| ota_0 | app/ota_0 | 0xD0000 | 0x260000 (2.375M) | The switched app slot. Meshtastic app is 2.03M today — fits with ~350K headroom |
| iwad | 66/6 | 0x330000 | 0x410000 (4.06M) | doom1.wad (4,196,020 B) — Doom finds it by type/subtype, not offset |
| pwad | 66/7 | 0x740000 | 0x60000 (384K) | prboom-plus.wad |
| spiffs | data/spiffs | 0x7A0000 | 0x60000 (384K) | Meshtastic littlefs. Its default is 1M, but prefs are tiny — **validate 384K works** (open question #2) |

Total: 0x800000 exactly. Numbers verified against the real binaries; the table is the project's foundation — validate it first (Phase 1).

**The 768K launcher budget is why we may NOT port bmorcelli's Launcher wholesale** — its builds run ~1.3–1.5 MB (WiFi, WebUI, catalog). See open question #1.

## Requirements

1. **Boot flow:** power-on → launcher runs → if a valid "last choice" exists and no button is held, boot it immediately (< 1 s added latency). Hold **UP** during reset → menu.
2. **Menu:** list installed app (ota_0 contents, with name), plus `.bin` files found on SD (`/firmware/*.bin`). D-pad navigate, A select, B back. Readable on the 320×240 TFT.
3. **Install from SD:** copy selected app `.bin` → ota_0 via `esp_ota_*`, with an on-screen progress bar; on success record the choice and reboot. Interrupted install must not brick: launcher is factory-partition resident and always reachable via hold-UP.
4. **Doom data handling:** if the selected firmware is Doom and the iwad/pwad partitions don't already hold the right WADs (hash or magic+size check), copy them from SD too (`/firmware/data/doom1.wad`, `prboom-plus.wad`) with progress. Skip when already present (the normal case — WADs stay resident).
5. **Meshtastic settings survive switching** (nvs + spiffs partitions never touched by installs).
6. **App bins on SD are plain app images** (not merged factory images) built against the shared partition table. The project must produce and document launcher-compatible builds of Doom and Meshtastic (see PLAN Phase 3) and ship them in `DefconBadge2026`.
7. Shared-bus hygiene: park CS GPIOs 47/48/39/37/14 before SD access; take the display CS only when drawing.

## Acceptance criteria

- [ ] From the menu, install & boot Meshtastic; verify with `meshtastic --port <PORT> --info` (protobuf API responds, `pioEnv` correct).
- [ ] From the menu, switch to Doom; verify title screen/demo on the TFT. Switch time ≤ 15 s when WADs already resident.
- [ ] Switch back to Meshtastic; **owner name / region set before the switch are intact**.
- [ ] Yank power mid-install; badge still boots to launcher menu; retry succeeds.
- [ ] A `.bin` dropped on the SD by a user shows up in the menu by filename and installs.
- [ ] Total added boot latency when not entering the menu < 1 s.

## Open questions (decide early, in this order)

1. **Launcher base: minimal custom vs stripped bmorcelli port.** Recommendation from prior work: **write minimal custom** on plain ESP-IDF. Rationale: every driver is already proven on this badge in ~small code (display: `spi_lcd.c` in the doom-badge repo, ~380 lines; SD: `DefconBadge2026/examples/sd-test`, ~150 lines; buttons: trivial GPIO). FATFS + esp_ota + a text menu fits in 768K easily. Porting Launcher's board abstraction + trimming it under 768K is likely *more* work. Revisit only if WebUI/catalog features are wanted.
2. **Is 384K spiffs enough for Meshtastic?** Prefs/nodedb are small (well under 100K in practice) but validate: flash Meshtastic with the shared table, configure it, reboot repeatedly, confirm no littlefs mount failures. If insufficient, shrink pwad's slack or launcher to grow spiffs.
3. **Where does otadata point on first boot?** Simplest: launcher always boots from factory (itself) and jumps to ota_0 via `esp_ota_set_boot_partition` + reboot, or loads it directly. Decide between "otadata points at ota_0, hold-button forces factory" (fast path, standard) vs "always boot factory, chain-load" (simpler logic, +300 ms). Standard otadata approach recommended.

## Out of scope (v1)

- OTA-over-WiFi/BLE, web UI, online catalogs
- Touch input (crossed pins; unverified bit-bang parked on doom-badge `touch-wip` branch)
- Updating the launcher itself from SD (USB reflash is fine for that)
- Non-ESP32 image formats, compression, signature verification
