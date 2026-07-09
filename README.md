# Badge Launcher

**Dual-boot firmware launcher for the Retia 2024 DEF CON badge.** Hold **UP** while tapping RESET and the badge boots into a menu; pick a firmware — installed in flash or a `.bin` on the micro-SD card — and it's running ~10 seconds later. No computer needed.

```
   _.---.._             _.---...__
.-'   /\   \          .'  /\     /
`.   (  )   \        /   (  )   /
  `.  \/   .'\      /`.   \/  .'
    ``---''   )    (   ``---''
            .';.--.;`.
          .' /_...._\ `.
        .'   `.a  a.'   `.
       (        \/        )
        `.___..-'`-..___.'
           \          /
            `-.____.-'
```

Ships with **DOOM** (silent + piezo-audio builds) and **Meshtastic 2.7.23** (standard + low-power builds). Any app-format `.bin` a user drops in `firmware/` on the card shows up in the menu.

## Quick start (a badge, an SD card, a USB cable)

1. **Flash the factory image once** (this wipes the badge, including Meshtastic settings):

   ```bash
   pip install esptool
   esptool --chip esp32s3 --port <PORT> write-flash 0x0 launcher.factory.bin
   ```

   The image contains the launcher, the shared partition table, both Doom WADs, and Meshtastic low-power as the pre-installed app. First boot shows the menu.

2. **Prepare the SD card** (FAT-formatted): copy the `firmware/` folder from the release's `sd-card.zip` onto the card root:

   ```
   /firmware/doom-silent.bin
   /firmware/doom-audio.bin
   /firmware/meshtastic-standard.bin
   /firmware/meshtastic-lowpower.bin
   /firmware/data/doom1.wad
   /firmware/data/prboom-plus.wad
   ```

3. **Use it**: hold **UP**, tap **RESET**, keep holding UP ~1 second → menu. D-pad to choose, **A** to install & boot, **B** to rescan the card. Switching apps takes ~10 s (the Doom WADs stay resident in flash, so they're only copied the first time or after corruption).

## How it works

The ESP32 can't execute code from SD, so "dual boot" is a fast reflash: the launcher copies the chosen `.bin` from the card into the `ota_0` flash partition (`esp_ota_*` APIs, which validate the image), records it as the boot app, and reboots.

**Boot flow** — normal power-on adds **zero latency**: the otadata partition points at `ota_0` and the second-stage bootloader boots your app directly. Holding UP through a reset triggers the bootloader's *factory-reset* path (`CONFIG_BOOTLOADER_FACTORY_RESET`, GPIO4 active-low, ≥1 s hold): it clears otadata **and nothing else** — the erase list is empty, so Meshtastic settings in `nvs`/`spiffs` survive — and boots the factory partition, which is the launcher.

**Un-brickable by construction**: the launcher lives in the factory partition and is never written after the initial USB flash. Yank power mid-install and the worst case is an invalid `ota_0` — the bootloader falls back to the launcher and you retry.

### The shared partition table (8 MB)

| Partition | Offset | Size | Holds |
|---|---|---|---|
| nvs | 0x9000 | 20 K | Meshtastic settings (never touched by installs) |
| otadata | 0xE000 | 8 K | Boot selection |
| launcher | 0x10000 | 768 K | This launcher (factory, permanent) |
| ota_0 | 0xD0000 | 2.375 M | **The switched app slot** |
| iwad | 0x330000 | 4.06 M | doom1.wad (stays resident) |
| pwad | 0x740000 | 384 K | prboom-plus.wad (stays resident) |
| spiffs | 0x7A0000 | 384 K | Meshtastic filesystem (never touched by installs) |

Every firmware in the menu must be **built against this table** (app ≤ 2,490,368 bytes, linked as a plain app image — not a merged factory image). The launcher rejects oversized files with an on-screen message.

## Adding your own firmware

Any ESP-IDF or Arduino-ESP32 app image works:

1. Point your project at [`launcher/partitions-shared.csv`](launcher/partitions-shared.csv) (`board_build.partitions` in PlatformIO, plus `board_build.app_partition_name = ota_0` so the size check uses the right slot).
2. Build, take the **app image** (`firmware.bin` — *not* `*.factory.bin`), drop it in `/firmware/` on the card.
3. It appears in the menu by filename. That's it.

Rules of the badge (see `docs/pinout.md`): park the five shared-bus chip-selects (GPIO 47/48/39/37/14 high) before touching SPI, use the dedicated SD slot (CS 10), and never ship a merged image with a QIO header.

**Doom note**: a bin whose filename contains `doom` triggers the WAD check after install — the launcher compares the resident WAD partitions against `/firmware/data/*.wad` (head + tail sample) and copies them from the card only when they differ.

## Updating firmware on the card

Overwrite the `.bin` on the SD card with the new build (same name is fine), hold-UP into the menu, and install it. The menu's "Boot" entry always shows the last-installed filename (stored in the launcher's own NVS namespace).

To update the **launcher itself**: USB reflash (`esptool write-flash 0x10000 launcher-app.bin`), or the full factory image for a clean slate.

### What about Meshtastic MUI (touch UI)?

The experimental MUI/LVGL touch build is **3.1 MB — it does not fit** the 2.375 MB app slot alongside the resident Doom WADs (3.1 + 4.4 + 0.77 > 8 MB, the math doesn't close). MUI stays a USB-flashed factory image with its own partition table; see `DefconBadge2026/firmware/meshtastic/`.

## Building the launcher from source

```bash
cd launcher
pio run -e defcon-badge          # app -> .pio/build/defcon-badge/firmware.bin
```

ESP-IDF 5.5 via PlatformIO; board definition in `launcher/boards/defcon-badge.json`. The app is ~330 K of its 768 K slot. See `HARDWARE.md` for the bench procedures (the native-USB DTR trap, the 1200-baud touch, recovery), and `docs/` for the badge pinout and SD notes.

## Bench automation (serial protocol)

The launcher speaks a line protocol on the USB-Serial/JTAG console (115200, **hold DTR asserted, never toggle it** — see the DTR trap in `HARDWARE.md`):

| Command | Reply | Purpose |
|---|---|---|
| `PING` | `PONG` | Liveness |
| `LIST` | `MENU <n>` + entries | Read the menu |
| `RUN <n>` | `RUNNING <n>` | Select entry n (install & boot) |
| `PUT <path> <size>` | `READY` … `OK <n>` | Write a file to the SD card (base64 lines, `K` ack every 8 — flow control is mandatory, the RX ring drops on overflow) |
| `DEL <path>` | `OK` | Delete a file on the card |

Host-side helper: `resources/push_file.py <port> <local> /sd/firmware/<name>` (~70 KB/s).

## Acceptance status

Hardware-verified on the bench (badge A, 2026-07-08): menu install & boot of both firmwares from SD; Doom switch in 9.4 s with WADs resident; Meshtastic owner/region intact across a full Doom round-trip and 9 reboots on the 384 K filesystem; hold-UP entry tested by hand; interrupted-install recovery exercised via mid-install reset.

## Credits

- Sitting-cats splash art by Felix Lee.
- Prior art: [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) and [tobozo/M5Stack-SD-Updater](https://github.com/tobozo/M5Stack-SD-Updater) — the menu-in-factory-partition pattern.
- PrBoom port and Meshtastic variants: see [RetiaLLC/DefconBadge2026](https://github.com/RetiaLLC/DefconBadge2026).
