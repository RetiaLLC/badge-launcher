# Badge Launcher v2 "Touch" — plan

*2026-07-09. Supersedes the staged draft at `~/Claude/Projects/Firmware Lab/LAUNCHER-V2-PLAN.md`.
Companion docs: this repo's v1 README/SPEC/HARDWARE, `MESHCORE-PORT-PLAN.md`, MUI report §11–13.*

**The pitch:** one USB flash, one SD card, and the badge becomes a *pick-what-you-run* device
with two full-color **touch mesh messengers** (MeshCore + Meshtastic MUI), classic Meshtastic,
an **NES emulator**, and LED art — switchable in ~10 s with three button presses, no computer.
It's the fun, hands-on way to learn mesh networking: two badges out of the box can find each
other and message over LoRa within minutes, then you swap firmware and see the *same radio*
speak a completely different mesh protocol.

Doom is the one thing that leaves: its 4.44 MB of resident WADs are what break the flash math.
Launcher **v1 stays published as "Classic"** (Doom-first); v2 is the "Touch" personality.
Same launcher codebase, two partition tables.

## 0. What v1 taught us (and how v2 uses it)

1. **Guest etiquette by convention is fragile; make it structural.** v1's rule was "never
   write `nvs`/`spiffs`" and the WLED guest had to be specially built FS-less to comply.
   Every firmware we've since wanted to host (MeshCore formats SPIFFS on fallback, Anemoia
   formats LittleFS on mount-fail) violates it *by default*. v2 gives every guest **its own
   labeled data partition** — isolation comes from the partition table, not from trust.
2. **The launcher was built table-agnostic and it paid off.** The install size check reads
   `ota0->size` from the live partition table ([main.c:307](launcher/src/main.c#L307)); the
   WAD sync already no-ops when the `iwad`/`pwad` partitions don't exist
   ([main.c:420-423](launcher/src/main.c#L420-L423)). Only the error *string* hardcodes
   "Max 2.4MB" ([main.c:308](launcher/src/main.c#L308)). v2 is a new CSV + tiny cosmetics,
   not a launcher rewrite.
3. **Un-brickable-by-construction and settings-survival are the product.** Factory-resident
   launcher, empty bootloader erase list (hold-UP clears *only* otadata), installs never
   touch data partitions. All carried forward unchanged — the adversarial swap test extends
   to three apps.
4. **Real numbers beat estimates.** v1's flash budget was validated before anything was
   built on it. §1 below is measured from actual binaries on disk today.
5. **Bench discipline:** USB port names are positional and move between plug events — always
   match by USB serial before flashing (see §7; there are two hands-off devices on the bus
   right now). The DUT's USB connector is degrading — use manual download mode
   (hold SW2/BOOT, tap SW1/RESET), `--before no-reset`.

## 1. Measured inputs (2026-07-09, binaries on disk)

| Image | Size (B) | Fits 3.25 MB slot? |
|---|---|---|
| Meshtastic MUI touch app (`retia-mui`, daa6a63) | **3,230,704** | ✓ headroom 177 KB |
| MeshCore touch messenger app (wadamesh `retia-badge`) | **2,703,296** | ✓ headroom 688 KB |
| Meshtastic standard / lowpower (`retia-power`) | 2,067,712 / 2,067,280 | ✓ |
| WLED pride (v1 recipe) | 1,231,248 | ✓ |
| **Anemoia NES emulator** (pioarduino env `badge`) | **1,117,600** | ✓ headroom 2.2 MB |
| v1 `ota_0` slot (for contrast) | 2,490,368 | MUI over by ~723 KB, MeshCore over by ~208 KB |

Anemoia guest research (verified in source):
- ROM path: finds a data partition **by name `nesrom`** (`flash_mmap.cpp:14`), erases/rewrites
  it only when the selected ROM's CRC32 differs, then read-only mmaps it. Offset-agnostic;
  **no launcher-side sync needed — the guest copies its own ROM from SD.**
- Needs: worst-case supported layout (512 K PRG + 256 K CHR + 8 B header) ≈ 768 KB. The
  36-title curated homebrew library is all well under.
- No NVS use at all; settings live in `/settings.bin` and save-states in `/states/` **on SD**.
- One hazard: `runtime_config.h:42-45` mounts LittleFS (default label → Meshtastic's
  partition) and **formats on mount-fail**. The bypass is a one-line change — the
  `// return cfg;` early-exit at `runtime_config.h:40` already exists in the source, and
  runtime settings come from SD anyway.
- There is **no PSRAM ROM backend** (only FLASH-mmap and the SD-streaming LRU that deadlocks
  the shared SPI bus mid-game), so the `nesrom` partition is required, not optional.

## 2. The v2 shared partition table (8 MB, sums to 0x800000 exactly)

```
# Name     Type  SubType   Offset    Size      Holds
nvs,       data, nvs,      0x9000,   0x5000,   # shared, namespaced (launcher name-cache, BLE bonds, app prefs)
otadata,   data, ota,      0xE000,   0x2000,   # boot selection
launcher,  app,  factory,  0x10000,  0x80000,  # 512 K (app ~330 K; v1 gave 768 K)
ota_0,     app,  ota_0,    0x90000,  0x340000, # 3.25 MB switched app slot  <-- the point
mcfs,      data, spiffs,   0x3D0000, 0x80000,  # 512 K MeshCore FS — MUST STAY THE FIRST subtype-spiffs ROW (see contract below)
spiffs,    data, spiffs,   0x450000, 0xC0000,  # 768 K Meshtastic LittleFS, found by NAME (2x the bench-proven 384 K)
nesrom,    data, 0x01,     0x510000, 0xC0000,  # 768 K Anemoia ROM store, found by NAME; guest self-writes from SD
tiles,     data, 0x83,     0x5D0000, 0x220000, # 2.125 MB LittleFS map-tile cache (label "tiles")
coredump,  data, coredump, 0x7F0000, 0x10000,
```

Every offset 64 K-aligned; app slot fits MUI with the font/emoji-trim lever (~200–270 KB,
spiked 2026-07-09) in reserve for future growth.

### Isolation model — labels *and order*, not trust

- **Meshtastic** (standard/lowpower/MUI): stock code mounts LittleFS with default label
  `"spiffs"` (explicit-by-name in the Arduino core header) → lands on the `spiffs` row
  wherever it sits. **Zero source changes.**
- **MeshCore/wadamesh**: calls `SPIFFS.begin()` with *default args* at ~8 sites
  (`main.cpp:444`, `main.cpp:526`, several in `UITask.cpp`). Arduino's SPIFFS default
  partition label is NULL → ESP-IDF resolves it to the **first subtype-`spiffs` partition
  in table order**. With `mcfs` ordered first, every one of those calls binds to `mcfs` —
  **zero source changes** (the staged draft's "one-line label change" is superseded by this;
  chasing all 8 sites is worse). Map tiles already mount by explicit label `tiles`.
- **⚠ THE ORDERING CONTRACT:** `mcfs` before `spiffs` in the CSV is load-bearing. It gets a
  shouting comment in the CSV header, and the Stage-2 swap test is the guard. Anyone who
  reorders the table reintroduces cross-app settings-wipe.
- **Anemoia**: owns `nesrom` (found by name); LittleFS bypassed by the one-line early-exit;
  everything else on SD.
- **NVS is shared and that's fine**: namespaced (`launcher` / Meshtastic / MeshCore keep
  separate namespaces and BLE bond stores). Watch item: 20 K NVS with two BLE stacks' bonds;
  coredump's 64 K is the annex if ever needed.
- The v1 invariant survives and gets stronger: **installs never touch data partitions, and
  now running apps can't cross-contaminate either.**

## 2b. Bench findings from the first build night (2026-07-09, all hardware-verified)

1. **IDF version is load-bearing for the display.** The launcher built on IDF 5.5.1
   (pioarduino 55.03.34, the machine's default resolution) runs perfectly — menu,
   SD scan, serial protocol, `esp_ota` installs — but the LCD stays BLACK. The same
   source on IDF 5.5.3 (v1's build) and 5.5.4 displays fine. Bisected on hardware by
   flashing the v1-built app onto the v2 table (displayed) vs the 5.5.1 rebuild
   (black). `launcher/platformio.ini` now pins pioarduino **55.03.39** (IDF 5.5.4)
   and documents this; never build the launcher on an unpinned platform.
2. **Guest app images are partition-table-agnostic.** ESP32 app images contain no
   partition offsets; they're MMU-mapped from whatever slot boots them and find
   filesystems by label/name at runtime. Consequence: the v1-built
   meshtastic-standard/lowpower/wled bins and the standalone MUI app bin run on v2
   **unchanged** — only guests needing *behavioral* deltas (MeshCore: hide self-OTA;
   Anemoia: skip LittleFS) needed rebuilds. The §4 table's "rebuild only" rows are
   ship-hygiene, not a functional requirement.
3. **The wadamesh mcfs isolation** shipped as the table-ordering contract (§2) plus a
   `WADAMESH_LAUNCHER_GUEST` source guard that blanks `FIRMWARE_OTA_ENV` (the
   `-U/-D` platformio flag dance corrupts the command line — don't).
4. **Toolchain traps on this machine**: bare `platform = espressif32` resolves to a
   tasmota-flavored IDF 4.4.8 (WLED work clobbered the unversioned framework dir), and
   an old registry `tool-esptoolpy` 4.5.1 shadows the v5 the platform wants (elf2image
   arg style changed) — both now pinned in platformio.ini.
5. **First live install verified end-to-end**: `RUN 3` over the bench serial protocol
   installed meshtastic-standard from the SD card into `ota_0` (~10 s) and booted it,
   on the v2 table, with the v2 bootloader. PING/PONG, LIST, and menu logging all good.
6. Menu scan now skips dotfiles (macOS drops `._foo.bin` AppleDouble sidecars on FAT
   cards; they'd appear as bogus menu entries).
7. The factory-preinstalled app shows as "Boot arduino-lib-bu…" until the first
   SD install writes a proper name to launcher NVS — cosmetic; fix candidate for v2.1
   (seed `app_name` in a factory NVS image or special-case the descriptor).

## 3. Launcher app changes (small, all verified against source)

1. New `launcher/partitions-shared-v2.csv` (above) + a second PlatformIO env or build flag →
   `launcher-v2.factory.bin`. One codebase, two personalities.
2. Error string at [main.c:308](launcher/src/main.c#L308): derive "Max X.XMB app slot" from
   `ota0->size`.
3. Friendly Doom refusal: WAD sync already no-ops without `iwad`, but a `*doom*.bin` would
   then install and boot into a missing-WAD error. Better UX: if the label contains "doom"
   and `esp_partition_find_first(66,6,NULL)` is NULL → on-screen "Doom needs Launcher
   Classic (v1)" and refuse the install.
4. Pre-installed app in the factory image: **meshcore-touch** (the flagship first-boot
   experience: touch wizard, name yourself, message a friend).
5. Everything else — menu, SD scan, `esp_ota_*` install path, hold-UP factory-reset entry,
   63-bin cap, serial bench protocol — unchanged.

## 4. Guest builds (each: `board_build.partitions` → v2 csv, `app_partition_name = ota_0`)

| Guest bin | Source | Deltas beyond the CSV swap |
|---|---|---|
| `meshcore-messenger.bin` | wadamesh `retia-badge` | none for storage (ordering contract covers it); set `FIRMWARE_OTA_ENV=""` + grey the self-OTA button (no `ota_1` in this table — esp_ota fails cleanly, but don't offer it) |
| `meshtastic-mui.bin` | meshtastic-mui `retia-mui` | rebuild only; **do not flash its 1.5 MB littlefs image** — first-boot format sizes to the 768 K partition; confirm no full-NVS-erase path in settings reset |
| `meshtastic-standard.bin` / `-lowpower.bin` | `retia-power` | rebuild only (offsets moved) |
| `anemoia-nes.bin` | RetiaLLC/Anemoia-DefconBadge | enable the `return cfg;` early-exit (`runtime_config.h:40`); optional polish: scan `/roms/` before falling back to card root, so ROMs don't clutter the SD root |
| `wled-pride.bin` | v1 recipe | rebuild only (already FS-less) |

⚠ v1 bins and v2 bins are **mutually incompatible** (offsets moved). Name loudly:
`sd-card-v2.zip`, and the launcher's installed-name display makes mixups visible.

## 5. The out-of-box learning experience (why this is fun)

SD card layout shipped in `sd-card-v2.zip`:

```
/START-HERE.txt            <- one page: what each firmware is, how to switch (hold UP + tap RESET),
                              "set your region before you mesh", antenna-before-TX warning
/MESH-101.md               <- the lesson: name your node, find your friend, first message;
                              then switch firmwares and watch the SAME radio join a DIFFERENT mesh —
                              MeshCore vs Meshtastic in practice, what a repeater is, why regions exist
/firmware/meshcore-messenger.bin
/firmware/meshtastic-mui.bin
/firmware/meshtastic-standard.bin
/firmware/meshtastic-lowpower.bin
/firmware/anemoia-nes.bin
/firmware/wled-pride.bin
/roms/*.nes                <- the 36-title validated homebrew library (staged at ~/Desktop/nes-roms-for-badge)
```

Out-of-box flow that must survive Stage 2 verification: flash two badges with
`launcher-v2.factory.bin` → both boot the MeshCore touch wizard → name yourselves → badge↔badge
message with ACKs in minutes. Then hold-UP, install `meshtastic-mui.bin`, set region on the
touch region picker (teachable moment: ISM bands), and message again on the other protocol.
Bored? Install the NES emulator and play until curiosity returns. Frequency defaults for both
messengers must match per-region docs (our MeshCore envs override the EU base defaults — verify
US 915 in the guest builds).

## 6. Stages

- **Stage 0 — measurements + spikes: DONE** (§1; real binaries + source verification, incl.
  the Anemoia guest research and the SPIFFS default-label semantics check).
- **Stage 1 — table + launcher (~an evening).** Author the v2 CSV; the three launcher tweaks
  (§3.1–3.4); build `launcher-v2.factory.bin`. Verify on the DUT: menu boots; install + boot
  `meshtastic-standard` from SD; set an owner; reboot cycle — settings survive; oversized-bin
  rejection shows the *derived* limit; `*doom*.bin` shows the Classic redirect.
- **Stage 2 — the flagships + the emulator (~an evening).** Build all five v2 guests.
  **Adversarial swap matrix:** MeshCore → MUI → NES → standard → MeshCore ×2 — each app keeps
  its own identity/settings/bonds; Meshtastic owner intact after MeshCore ran (the ordering
  contract's proof); NES ROM CRC-skip works on re-entry; SD card intact throughout; BLE
  pairing works in both messengers after swaps. Two-badge RF check after the matrix (badges
  `…4A:F0`/`…4A:F8` are on the bench for this).
- **Stage 3 — ship.** DefconBadge2026 release `launcher-v2.0.0`: `launcher-v2.factory.bin` +
  `sd-card-v2.zip` (§5, **no Doom bins**) + START-HERE/MESH-101. README: "two launcher
  personalities — Classic (Doom) vs Touch (mesh + games)" table. This repo gets the v2 CSV,
  a README section, and this plan checked off.
- **Stage 4 — optional/v2.1.** (a) Font/emoji-trimmed MUI if the slot tightens. (b) PSRAM ROM
  backend upstreamed to Anemoia — would free the 768 K `nesrom` back to tiles. (c) The
  `<name>.dat → data partition` convention for future data-hungry guests. (d) Touch input in
  the launcher menu itself (bit-bang XPT2046 is proven, but it once killed the whole SPI bus
  via a wrong pin-restore — d-pad works; only do this with the §7 rails). (e) Doom-from-SD
  investigation for a possible personality merge (likely a no — the WAD wants mmap).

## 7. Bench state + safety rails (as of 2026-07-09 evening)

- **DUT badge `74:4D:BD:23:4B:08`** is on local Mac USB running Anemoia — it's the v2 test
  unit. Flashing the launcher wipes Anemoia; that's fine (it ships standalone on
  DefconBadge2026, and it comes *back* as a guest in Stage 2).
- **HANDS OFF: `F0:F5:BD:6D:3E:C8` and `F0:F5:BD:6D:4D:6C`** — two ESP32-S3 nuggets on this
  Mac's USB under separate development. Same "USB JTAG/serial debug unit" identity as the
  badge. **Always resolve the port by USB serial via `ioreg` immediately before any esptool
  invocation**; never flash by positional port name.
- DUT USB connector is degrading: no automated DTR resets — manual download mode
  (hold SW2/BOOT, tap SW1/RESET, release SW2), `esptool --before no-reset --after hard-reset`.
  Full procedures in `HARDWARE.md` and the `retia-badge-workbench` skill.
- Badges `…4A:F0` (wadamesh, good screen) and `…4A:F8` (cracked digitizer, MeshCore-companion
  history) are the two-badge RF pair for Stage 2; reflash as needed per their current owners'
  notes.

## 8. Risks

| Risk | Mitigation |
|---|---|
| MUI headroom only ~177 KB | font/emoji-trim lever (~200–270 KB, already spiked); or shave launcher slot to 448 K |
| The `mcfs`-first ordering contract gets silently broken later | shouting CSV comment + the Stage-2 swap matrix is a hard gate for any table change |
| Two BLE stacks' bonds in 20 K NVS | namespaced, measured fine today; coredump's 64 K is the annex |
| A ROM over 786,424 B (nesrom minus header) | library is validated well under; Anemoia's partition-write failure is non-destructive to other apps; document the limit next to the ROM folder |
| Users flash a v1 bin on v2 (or vice versa) | offsets moved → app boots but finds wrong/no FS; loud zip naming + launcher shows installed-image name |
| MUI first boot vs 768 K littlefs (its standalone table gave 1.5 MB) | Stage-2 verify item: first-boot format + settings persistence at 768 K (v1 proved standard-Meshtastic happy at 384 K) |
| "Where's Doom?" | Launcher Classic (v1) stays published; Doom factory images unchanged on DefconBadge2026; explicit on-screen redirect (§3.3) |
