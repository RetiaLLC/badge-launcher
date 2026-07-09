# Badge Launcher v2 — dual TOUCH messengers (Meshtastic MUI + MeshCore)

*2026-07-10. Companion docs: RetiaLLC/badge-launcher (v1), MESHCORE-PORT-PLAN.md.
Goal: a launcher personality where the SD-card menu offers BOTH full-color touch
messengers. Doom leaves this image (its 4.44 MB of resident WADs are what break
the math) — Doom stays available as the standalone factory images already on
DefconBadge2026, and launcher v1 "classic" remains for Doom-first users.*

## 0. Why v1 cannot host the touch builds (measured, not estimated)

| Fact | Number |
|---|---|
| v1 switched app slot (`ota_0`) | **2,490,368 B** (0x260000) |
| MeshCore touch app image (today) | **2,703,296 B** → ~208 KB over |
| Meshtastic MUI app image | **~3.1 MB** → ~630 KB over, no realistic diet |
| v1 resident Doom data (`iwad`+`pwad`) | 4.44 MB — the space the slot can't grow into |

Two independent blockers beyond size:

1. **Storage etiquette.** v1 guests must never write `nvs`/`spiffs` (they hold
   Meshtastic's settings). Every MeshCore build *formats and writes* SPIFFS
   (`SPIFFS.begin(true)` would format Meshtastic's LittleFS on first mount →
   settings wiped) and writes NVS (touch prefs).
2. **Spike results (2026-07-10, bench):**
   - *WiFi-strip shrink is NOT a flag flip*: building wadamesh without
     `MULTI_TRANSPORT_COMPANION` breaks dozens of UI sites (WiFi settings pages,
     scan popup, tile fetcher, BLE-PIN UI). WiFi is load-bearing; a no-WiFi guest
     is a gating project, not a build variant.
   - *Font/emoji trim is the real shrink lever*: the color-emoji table +
     non-Latin fallback fonts (`emoji_data.c` 700 KB src, `extras_font_12/14/16`
     ~900 KB src) compile to roughly **200–270 KB** — enough to squeeze under the
     v1 slot, but the storage blocker still stands, so it's a *headroom lever for
     v2*, not a v1 fix.
   - *Launcher code is nearly v2-ready*: the install size check reads
     `ota0->size` from the partition table at runtime (only the error STRING
     says "Max 2.4MB app slot"); menu/installer are table-agnostic.

## 1. v2 design

### Partition table (8 MB, 64 K-aligned)

```
# Name     Type  SubType   Offset    Size      Holds
nvs,       data, nvs,      0x9000,   0x5000,   # shared, namespaced (BLE bonds, app prefs)
otadata,   data, ota,      0xE000,   0x2000,   # boot selection
launcher,  app,  factory,  0x10000,  0x80000,  # 512 K (app is ~330 K today; v1 gave it 768 K)
ota_0,     app,  ota_0,    0x90000,  0x340000, # 3.25 MB switched app slot  ← the point
mtfs,      data, spiffs,   0x3D0000, 0xC0000,  # 768 K, LABEL "spiffs" → stock Meshtastic mounts it unmodified (2x v1)
mcfs,      data, spiffs,   0x490000, 0x100000, # 1 MB, LABEL "mcfs" → MeshCore prefs/contacts/chat history
tiles,     data, 0x83,     0x590000, 0x260000, # 2.4 MB LittleFS map-tile cache (MeshCore map; label "tiles")
coredump,  data, coredump, 0x7F0000, 0x10000,
```

Fit check: MUI 3.1 MB ≤ 3.25 MB (≈150 KB headroom — the font-trim lever covers
future growth); MeshCore touch 2.70 MB with ~550 KB to spare, so it keeps its
full WiFi stack (phone-over-TCP, web client, MQTT) even as a guest.

### Isolation model — labels, not trust

- **Meshtastic** keeps partition label `spiffs` → stock/MUI builds mount their
  LittleFS **unmodified**. Nothing MeshCore does can touch it.
- **MeshCore** mounts by label: `SPIFFS.begin(true, "/spiffs", 10, "mcfs")` —
  a one-line change in our wadamesh branch. Map tiles already mount by label
  `tiles` (the code shipped that way; the partition simply exists now).
- **NVS is shared and that's safe**: NVS is namespaced; Meshtastic (NimBLE) and
  MeshCore (Bluedroid) keep separate bond stores and separate pref namespaces.
  Watch item: 20 K NVS with two apps' bonds — can grow into coredump's 64 K if
  ever needed.
- The v1 invariant survives: **installs never touch data partitions**, and now
  *running apps* can't cross-contaminate either, because each has its own home.

### Launcher app changes (small, verified against source)

1. New `partitions-shared-v2.csv` + factory image build.
2. Error string "Max 2.4MB app slot" → derive from `ota0->size`.
3. WAD-sync path: gate on the `iwad` partition existing (dormant in v2, keeps
   one launcher codebase for both personalities).
4. Pre-installed app in the factory image: **meshcore-touch** (the flagship).
5. Everything else (menu, SD scan, `esp_ota_*` install, hold-UP factory-reset
   boot flow, un-brickable factory-partition property) is unchanged.

### Guest builds (each = `board_build.partitions` → v2 csv, `app_partition_name = ota_0`)

| Guest | Source | Deltas |
|---|---|---|
| `meshcore-touch.bin` | wadamesh `retia-badge` | SPIFFS label `mcfs`; `FIRMWARE_OTA_ENV=""` + grey the self-OTA button (no `ota_1` in this table — esp_ota fails cleanly, but don't offer it) |
| `meshtastic-mui.bin` | meshtastic-mui `retia-mui` | rebuild against v2 csv; FS label already `spiffs`; confirm no full-NVS-erase path in settings reset |
| `meshtastic-standard.bin` / `-lowpower.bin` | retia-power | rebuild against v2 csv (offsets moved) |
| `wled-pride.bin` | v1 recipe | rebuild against v2 csv (already FS-less) |

## 2. Stages

- **Stage 0 — measurements + spikes: DONE** (this doc §0; numbers are from real
  builds and the launcher source, not estimates).
- **Stage 1 — table + launcher (bench: ~an evening).** Author v2 csv; the three
  launcher tweaks; build `launcher-v2.factory.bin`. Verify: menu boots, install
  + boot `meshtastic-standard` guest, set an owner, reboot cycle — settings
  survive; oversized-bin rejection shows the derived limit.
- **Stage 2 — the two flagships (bench: ~an evening).** `mcfs` label + OTA-UI
  gating commit in wadamesh; MUI guest build from `retia-mui`. Adversarial swap
  test: A→B→A→B×3 — each app keeps its own identity/settings/bonds; SD card
  survives; BLE pairing works in both after swaps.
- **Stage 3 — ship.** DefconBadge2026 release `launcher-v2.0.0`:
  `launcher-v2.factory.bin` + `sd-card-v2.zip` (both messengers + standard/
  lowpower + WLED, **no Doom bins**). README: "two launcher personalities —
  classic (DOOM) vs touch (both messengers)" table. badge-launcher repo gets
  the v2 csv + README section.
- **Stage 4 — optional.** (a) Doom-from-SD investigation (stream/copy WAD at
  install; likely a no — mmap'd WAD wants flash). (b) Font/emoji-trimmed MUI
  or MeshCore variants if the slot ever gets tight. (c) The v1-floated
  `<name>.dat → data partition` convention for future data-hungry guests.

## 3. Risks

| Risk | Mitigation |
|---|---|
| MUI headroom only ~150 KB | font/emoji trim lever (~200–270 KB); or shave launcher slot to 448 K |
| Two BLE stacks' bonds in 20 K NVS | namespaced, measured fine today; can annex coredump's 64 K |
| MeshCore chat history vs 1 MB `mcfs` | ~110 KB today; tiles have their own partition; history ring is capped |
| Users flash a v1 guest bin on v2 (or vice versa) | offsets moved → app boots but mounts wrong/no FS; name the zips loudly (`sd-card-v2.zip`), launcher shows installed-image name |
| "Where's Doom?" | launcher-v1 stays published as *classic*; Doom factory images unchanged on DefconBadge2026 |
