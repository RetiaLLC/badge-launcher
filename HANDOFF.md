# badge-launcher — project handoff

SD-card firmware launcher ("dual boot") for the Retia 2024 DEF CON badge. This folder is a self-contained starting point for the agent building it.

**Read in this order:**

1. [SPEC.md](SPEC.md) — what to build, the flash-budget math (already worked out), acceptance criteria, open questions
2. [PLAN.md](PLAN.md) — phased plan with hardware checkpoints, risks, ship list
3. [HARDWARE.md](HARDWARE.md) — the two badges on the bench, flashing/console procedures, every known trap and its recovery

**Resources:**

- `resources/defcon-badge.json` — PlatformIO board definition (proven)
- `resources/partition-tables/shared-proposed.csv` — the proposed shared table (validate first!); `doom-current.csv` / `meshtastic-current.csv` for what the firmwares use today
- `resources/read_console.py` — DTR-safe serial console reader (`python3 read_console.py <seconds> <port>`)
- `resources/docs/` — badge pinout + quirks, flashing guide, SD guide (copies from the DefconBadge2026 repo)
- `upstream/Launcher/` — shallow clone of bmorcelli/Launcher for reference (prior art; SPEC recommends a minimal custom loader instead, but steal patterns freely)

**Context:** Doom port and both Meshtastic builds already run on this badge and ship at github.com/RetiaLLC/DefconBadge2026. The launcher's job is to let users switch between them (and their own bins) from an SD card, no computer needed. Everything hardware-risky about this badge has been hit and documented already — trust the docs, they were paid for in boot loops.
