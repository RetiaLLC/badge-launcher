# Badge Launcher

**Firmware launcher for the Retia DEF CON badge.** Hold **UP** while tapping RESET and the badge boots into a menu; pick a firmware — installed in flash or a `.bin` on the micro-SD card — and it's running ~10 seconds later. No computer needed.

**⚠ A micro-SD card (FAT-formatted) is required** for both launcher versions — it's where the firmware library lives.

Two personalities, one codebase — pick your flavor:

| | **v2 "Touch"** (flagship) | v1 "Classic" |
|---|---|---|
| Mesh messengers | **MeshCore touch** + **Meshtastic MUI touch** + standard/low-power | Meshtastic standard/low-power |
| Games | **NES emulator** (Anemoia) + a 37-title homebrew library | **DOOM** (WADs resident in flash) |
| More | **Reticulum RNode**, WLED pride | WLED pride |
| App slot | 3.25 MB | 2.375 MB |
| Release | [`v2.0.0`](../../releases/tag/v2.0.0) | [`v1.0.0`](../../releases/tag/v1.0.0) |

## v2 "Touch" quick start

1. Flash the factory image once (wipes the badge):
   ```bash
   pip install esptool
   esptool --chip esp32s3 --port <PORT> write-flash 0x0 launcher-v2.factory.bin
   ```
   …or flash it straight from your browser at [scriptkitty.sh](https://scriptkitty.sh).
2. Unzip **`sd-card-apps-v2.zip`** onto a FAT micro-SD card (required), and **`sd-card-games-v2.zip`** too if you want the NES library.
3. First boot lands in the menu with the **MeshCore touch messenger** pre-installed. Read `START-HERE.txt` on the card, then `MESH-101.md` to get two badges talking over LoRa in minutes.

Every guest keeps its own private flash partition (`partitions-shared-v2.csv`) — your Meshtastic identity, MeshCore contacts, RNode provisioning, and NES saves all survive switching. Design + bench history: [PLAN-v2-touch.md](PLAN-v2-touch.md).

## Switching & adding guests (v2)

**Switch firmware (no computer):** hold **UP**, tap **RESET**, keep holding UP ~1 s → the menu appears. **D-pad** to move, **A** to install & boot the highlighted app, **B** to rescan the card. A switch takes ~10 s (the launcher copies the chosen `.bin` from `/firmware/` on the card into the `ota_0` flash slot via `esp_ota`, records it, and reboots). Normal power-on boots the last app directly with zero added latency; the launcher is only reached via hold-UP and is never overwritten, so an interrupted install can't brick the badge.

### Add your own guest firmware

Any ESP-IDF or Arduino-ESP32 app works, as long as it's built to live in the shared v2 layout:

1. **Build against [`launcher/partitions-shared-v2.csv`](launcher/partitions-shared-v2.csv)** — `board_build.partitions` in PlatformIO, plus `board_build.app_partition_name = ota_0` so the size check uses the right slot. The v2 app slot is **`ota_0` @ 0x90000, 3.25 MB**.
2. **Ship the plain app image** (`firmware.bin`) — **not** a `*.factory.bin`. The launcher installs via `esp_ota`, which validates the app descriptor; a merged factory image is rejected (safely — no brick).
3. **Drop it in `/firmware/*.bin`** on the card. It shows up in the menu by filename.

### Be a good guest ⚠ (v2 has *two* filesystems)

The v2 table carries two `spiffs`-subtype partitions and a strict ordering contract:

| Partition | Owner | How it's found |
|---|---|---|
| `mcfs` (first spiffs row) | MeshCore / wadamesh | `SPIFFS.begin()` with **default args** → ESP-IDF resolves the NULL label to the **first** spiffs-subtype partition |
| `spiffs` | Meshtastic (LittleFS) | by **name** (order-immune) |
| `nesrom`, `tiles`, `nvs` | Anemoia / map tiles / launcher | by name / type |

So a *running* guest that mounts-and-formats a partition it doesn't own will **wipe another app's settings** (e.g. WLED's default `LittleFS.begin("spiffs")` would reformat Meshtastic's storage). A well-behaved guest:

- **Never writes a partition it doesn't own.** If your app needs no persistence, point its filesystem at a **nonexistent partition label** (FS-less) so it mounts nothing. If it does need storage, add your own dedicated data partition to a custom table — don't borrow `mcfs`/`spiffs`.
- **Fits `ota_0`** (≤ 3.25 MB) — oversized bins are rejected on-screen.

**Reference guest — `wled-pride.bin` / WLEDkitty:** WLED normally reformats `spiffs` on boot. Its launcher build is compiled **FS-less** (LittleFS pointed at a nonexistent label) with its config baked in, so it never touches shared storage. Verified on the v2 launcher: set a Meshtastic owner → install & boot WLEDkitty → switch back → the owner is intact.

### Bench / automation (serial protocol)

The launcher exposes a line protocol on the USB-Serial/JTAG console (115200; hold DTR asserted, never toggle it — see [HARDWARE.md](HARDWARE.md) on the native-USB DTR trap):

| Command | Reply | Purpose |
|---|---|---|
| `PING` | `PONG` | liveness |
| `LIST` | `MENU <n>` + entries | read the menu |
| `PUT <path> <size>` | `READY` … `OK <n>` | write a file to the SD card (base64 lines; `K` ack every 8 lines — flow control is mandatory) |
| `RUN <n>` | `RUNNING <n>` | install & boot menu entry *n* |

Host helper: `resources/push_file.py <port> <local.bin> /sd/firmware/<name>.bin`. To reach the launcher from a running native-USB guest (WLED/Meshtastic ignore esptool's reset), do the **1200-baud touch** to drop into ROM, erase otadata, and let the factory launcher boot — or physically hold **BOOT**, tap **RESET**.

---

The sections below document **v1 "Classic"** (Doom-first) — still published and maintained.

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

Ships with **DOOM** (silent + piezo-audio builds), **Meshtastic 2.7.23** (standard + low-power builds), and **WLED** (boots the ear NeoPixels straight into a Pride rainbow). Any app-format `.bin` a user drops in `firmware/` on the card shows up in the menu.

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
   /firmware/wled-pride.bin
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

The menu holds up to **63 firmware bins** from the card (`MAX_ENTRIES` in `launcher/src/main.c`) and scrolls; the card itself is the only real limit on how many you keep around, since only one is flash-resident at a time.

### Guest-firmware etiquette

Firmwares share three flash regions with their neighbors. A well-behaved guest:

- **Never writes `spiffs` or `nvs`.** These hold Meshtastic's settings (owner, region, keys) and the launcher's own state. Installs never touch them, but a *running* app that formats or writes them will clobber another firmware's config. If your app needs persistent storage, keep it inside your own app image's footprint or use a dedicated data partition.
- **Only Doom gets resident data today.** The 4.06 MB `iwad` + 384 K `pwad` partitions are auto-synced from the card only for bins whose name contains `doom`. Any other firmware must fit its assets inside its 2.375 MB app image — the launcher won't copy side files for it. (A general `<name>.dat → data partition` convention is a natural v1.1 if a second data-hungry app shows up.)
- **Fits the slot**: app image ≤ 2,490,368 bytes. Oversized bins are rejected on-screen, not silently truncated.

The bundled **`wled-pride.bin`** is the reference well-behaved guest: WLED normally reformats the `spiffs` partition on boot, which would wipe Meshtastic's settings. Its launcher build is compiled FS-less (LittleFS pointed at a nonexistent partition) with the Pride rainbow baked in as the boot default — so it never touches shared storage and needs no runtime config. Verified: setting a Meshtastic owner, switching to WLED and back, leaves the owner intact.

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
