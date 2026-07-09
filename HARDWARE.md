# Bench State & Hardware Procedures

Everything a fresh agent needs to work with the two badges on this desk without re-learning the traps. Complementary deep docs: `resources/docs/pinout.md` (GPIO map + quirks), `flashing.md`, `sd-card.md`.

## The two badges (state as of 2026-07-08)

| Badge | MAC / short name | USB port (when running Meshtastic) | Current firmware | Notes |
|---|---|---|---|---|
| A | 74:4d:bd:23:4b:08 "4b08" | `/dev/cu.usbmodem744DBD234B081` | Meshtastic 2.7.23 **standard** (`defcon-badge` env), region US, fresh settings | **Has the 16 GB SDHC card** (FAT, contains `/badge_test.txt`). 915 MHz antenna attached. |
| B | 74:4d:bd:21:64:94 "6494" | `/dev/cu.usbmodem744DBD2164941` | Meshtastic 2.7.23 **low-power** (`retia_dcbadge` env), region US, fresh settings (full-erased 07-08) | Antenna attached. Kody may be running power measurements on it — **ask before reflashing badge B**. |

Port names change with firmware: Meshtastic (TinyUSB CDC) → `usbmodem<MAC>`; ROM bootloader or IDF-native firmware (USB-Serial/JTAG) → generic `usbmodem1101`/`usbmodem101`-style. Don't glob blindly — two badges are attached.

## Flashing

```bash
source ~/firmware-lab/.venv/bin/activate     # esptool v5, pio, meshtastic CLI
esptool --port <PORT> --chip esp32s3 --before default-reset --after hard-reset write-flash 0x0 <factory.bin>
```

- Always `--after hard-reset` (never `no-reset` — leaves the badge in download mode).
- **Merged images must be built `--flash-mode dio`.** A QIO bootloader header boot-loops the ROM (`rst:0x7 TG0WDT` + `ets_loader.c 78`). Recovery: reflash bootloader.bin at 0x0.
- Badge running **Meshtastic** ignores esptool's reset. Do the **1200-baud touch**: open its CDC port at 1200 baud with DTR/RTS low, close; the badge re-enumerates as a generic ROM port within ~10 s; flash that. (esptool attempts sometimes trip it into ROM as a side effect — check `ls /dev/cu.usbmodem*` before retrying.) Physical fallback: hold BOOT (SW2, silk "GPIO_0"), tap RESET (SW1), release.

## Serial console (the DTR trap)

The S3's native USB wires host DTR toward GPIO0 — a monitor that toggles DTR + a reset = badge stuck "waiting for download". Rules:

- Read the console with `resources/read_console.py <seconds> <port>` — it holds DTR steadily asserted and never toggles it, and survives re-enumeration. Console output requires firmware built with `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.
- One client per port; kill any reader (`pkill -f read_console.py`) before esptool.
- Un-trap a badge stuck in download: `esptool --port <PORT> --chip esp32s3 flash-id` (exits via hard reset → app boots).

## Meshtastic testing

- `meshtastic --port <PORT> --info` — read-only, safe, no TX. The USB CDC carries the protobuf API, not text logs.
- **Never enable TX (set a region) on a badge without a 915 MHz antenna.** Both bench badges currently have antennas (confirmed by Kody 07-08).
- Two-badge RF check: `python3 ~/.claude/skills/retia-badge-workbench/scripts/send_recv.py <rx_port> <tx_port> "MSG"` → expect `RESULT: PASS` with rxRssi ≈ −15…−25 dBm at desk distance.

## Build environment

- Toolchain: PlatformIO (`pio run -e defcon-badge`) building ESP-IDF **5.5.3**; first build on a fresh machine may fail on PlatformIO's IDF penv — fix: `~/.platformio/penv/.espidf-5.5.3/bin/python -m pip install idf-component-manager -r ~/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt`.
- Board definition: `resources/defcon-badge.json` (8 MB flash QIO, 2 MB quad PSRAM, 240 MHz).
- Key repos on this machine:
  - **Doom**: `/Users/skicka/Documents/Gemini/doom-badge` — branch `defcon-badge-doom`, tags `stable-v1` (audio) / `stable-v2` (silent); WADs in `wads/`; touch experiment on `touch-wip` (unverified).
  - **Meshtastic**: `~/firmware-lab/meshtastic-firmware` — badge env `retia_dcbadge` on branch `retia-power` (low-power build); the standard build used env `defcon-badge` (variant files also in `DefconBadge2026/firmware/meshtastic/`).
  - **Release repo**: `/Users/skicka/Documents/Gemini/DefconBadge2026` → github.com/RetiaLLC/DefconBadge2026 (gh authed as `skickar`). Working examples: `examples/sd-test` (SD, hardware-verified), `examples/hello-badge` (buttons/LED/buzzer).
- Current partition tables for reference: `resources/partition-tables/doom-current.csv`, `meshtastic-current.csv`.

## SD card facts (hardware-verified)

Dedicated slot only (CS GPIO10; the display-module slot CS39 has crossed data lines — dead). SDSPI @ 20 MHz on the shared bus, FAT. Park CS 47/48/39/37/14 high first. Working reference code: `DefconBadge2026/examples/sd-test/src/main.c`.
