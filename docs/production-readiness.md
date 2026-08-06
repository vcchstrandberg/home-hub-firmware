# Production Readiness — Provisioning and OTA

Two gaps prevent this solution from being deployable outside a single home without a laptop and USB cable: WiFi provisioning and firmware updates. This document outlines practical approaches for both.

---

## 1. WiFi and device provisioning

### The problem

WiFi credentials, proxy host/IP, and device name are hardcoded in `arduino_secrets.h` at compile time. Deploying to a new location means editing the file, rebuilding, and flashing over USB.

### Recommended approach: captive portal (WiFiManager)

This is the industry standard for consumer IoT devices — it is how Shelly, Sonoff, Philips Hue bridges, and most commercial ESP32 products handle first-time setup.

**Flow:**

1. On first boot (or when the device cannot connect to its saved network), it starts in **AP mode** broadcasting an SSID like `Netatmo-Setup-CAM`.
2. The user connects to that hotspot from any phone or laptop — no app needed.
3. A captive portal opens automatically (or the user navigates to `192.168.4.1`).
4. A simple web form collects: WiFi SSID, WiFi password, proxy host, proxy port, device name.
5. The device saves these values to **NVS** (non-volatile storage in flash — survives reboots and power loss) and reboots in station mode.
6. Done. The device is configured and the USB cable is never needed again.

**Reset:** Holding the BOOT button for 3+ seconds clears NVS and re-enters AP mode, allowing reconfiguration without reflashing.

**Library:** [`WiFiManager` by tzapu](https://github.com/tzapu/WiFiManager) has full ESP32 support and PlatformIO availability. It handles the AP, DNS, HTTP server, and form entirely — the integration into `main.cpp` is roughly 20 lines.

**What changes in the firmware:**
- `arduino_secrets.h` is no longer needed for WiFi credentials or proxy config.
- On boot, read config from NVS. If empty, start AP mode.
- The BOOT button gains a secondary role: hold 3 s → reset config.

**Alternative: SD card config (ESP32-CAM only)**

The ESP32-CAM has a microSD slot. A `config.txt` or `config.json` on the card could supply all settings, making provisioning as simple as writing a text file and inserting the card. No network needed at all. Less elegant for non-CAM boards but worth noting for the CAM specifically.

---

## 2. Over-the-air firmware updates

Scoped to the **ESP32-family targets only** (`esp32cam`, `esp32dev`, `esp32c6_zero`, `esp32c6_zero_ili9341`, `esp32c6_waveshare_lcd`, `esp32s3_waveshare_lcd`). The Uno R4 WiFi (Renesas RA4M1) has a different OTA story — `ArduinoOTA` supports it, but it's less battle-tested than the ESP32 path and is treated separately.

### The problem

Firmware updates require physically connecting a USB cable, running `pio run -e <env> --target upload`, and being present at the device. At scale or in remote locations this is impractical.

### Phase 0 — Partition table migration (prerequisite, one-time USB reflash per board)

Investigated 2026-08-06 by inspecting the actual partition CSVs and board flash sizes rather than assuming. Findings:

- **`huge_app.csv`** (currently used by `esp32c6_waveshare_lcd`, `esp32c6_zero_ili9341`, `esp32s3_waveshare_lcd`) defines an `otadata` partition and one `app0` slot typed `ota_0` — but there is **no second app slot**. `Update.h` could technically overwrite `app0` in place today, but there's no A/B fallback: a bad image bricks the device until it's walked back to a USB port. Not a foundation to build real OTA on.
- **`default.csv`** (the implicit default for `esp32cam` / `esp32dev`, since they set no `board_build.partitions`) already has two 1.25 MB `ota_0`/`ota_1` slots — those two boards are already OTA-shaped, partition-wise.
- Flash headroom is generous and doesn't require any hardware change: the C6 boards' actual chip is **8 MB** (confirmed against Waveshare's spec), and `huge_app.csv` only uses the first 4 MB of it. The S3 board is **16 MB** (`board_upload.flash_size = 16MB` already set).
- Current firmware sizes (this session's builds): C6 Touch LCD 1.48 MB, S3 LCD-2.8 1.35 MB, C6-Zero+ILI9341 1.20 MB, C6-Zero 1.15 MB, ESP32-CAM 1.08 MB — all fit comfortably inside a ~2 MB+ app slot.
- **A partition table change can only be applied via a normal USB flash** — PlatformIO always writes the full image (bootloader + partition table + app) on upload, so this rides along with one ordinary `--target upload`, but it can't be pushed over OTA itself (the new layout has to exist before OTA can use it). This is the one unavoidable cable trip; after it, routine updates no longer need one.

**Per-board plan:**

| Target | Today | Migrate to | Why |
|---|---|---|---|
| `esp32s3_waveshare_lcd` | `huge_app.csv` (single 3 MB slot) | `default_16MB.csv` (two 6.25 MB slots, built into arduino-esp32 — no custom CSV needed) | 16 MB chip, current binary 1.35 MB — enormous headroom either way |
| `esp32c6_waveshare_lcd` | `huge_app.csv` (single 3 MB slot) | Custom two-slot table sized for the real 8 MB chip (e.g. two ~3 MB slots) | No off-the-shelf scheme fits an 8 MB chip well; `default_16MB.csv`'s slots don't fit in 8 MB |
| `esp32c6_zero_ili9341` | `huge_app.csv` (single 3 MB slot) | Same custom 8 MB two-slot table as above | Same C6 chip |
| `esp32cam` / `esp32dev` | `default.csv` (two 1.25 MB slots) | Optionally `min_spiffs.csv` (two 1.875 MB slots) | Already OTA-shaped; `esp32cam` is at 86% of its current 1.25 MB slot, worth the extra room since neither board uses SPIFFS |
| `esp32c6_zero` | implicit default | Confirm it's landing on a two-slot scheme; adjust if not | Not yet checked |

Done for `esp32s3_waveshare_lcd` as of v2.8 (`board_build.partitions = default_16MB.csv`). The rest are not yet migrated.

### Phase 1 — Arduino OTA (same network, no cable)

ESP32 has built-in OTA support. Once enabled, `pio run -e <env> --target upload --upload-port <device-ip>` pushes firmware over WiFi — no cable needed. Requires Phase 0 (a two-slot partition table) to actually be safe.

**Limitation:** requires the developer's machine to be on the same local network as the device. Good enough for home use, not for remote deployment.

**Implementation note:** the codebase has a few synchronous busy-wait loops (boot splash, `showLocale()`) that don't pump anything else while blocked. `ArduinoOTA.handle()` in `loop()` won't run during those few seconds — acceptable (OTA just isn't reachable mid-splash/mid-locale-cycle), but worth calling out rather than discovering it as a bug later.

**Env pattern:** `upload_protocol = espota` is set on a *separate* PlatformIO env (`esp32s3_waveshare_lcd_ota`, via `extends = env:esp32s3_waveshare_lcd`) rather than on the base env. Setting it on the base env would make plain `-e esp32s3_waveshare_lcd --target upload` try to reach the device over WiFi by default — breaking the first install (the ArduinoOTA listener isn't running yet) and removing the USB fallback. PlatformIO Core 6.1.19's `extends` needs the `env:` prefix (`extends = env:esp32s3_waveshare_lcd`, not just the bare name) — the bare form silently drops the inherited `platform`/`board` and fails with `UndefinedEnvPlatformError`.

**Verified on hardware 2026-08-06:** flashed the ArduinoOTA-enabled build via USB once (required — the listener has to already be running before anything can reach it over WiFi), read the device's DHCP IP off serial, then pushed the same build again via `pio run -e esp32s3_waveshare_lcd_ota --target upload --upload-port <ip>`. Transfer completed (`Result attempt 1: 'OK'`), device rebooted on its own, and reconnected to WiFi and the hub normally — confirmed by reading boot serial after a forced reset. Currently unauthenticated (no `ArduinoOTA.setPassword()`) — fine for this same-LAN verification, but add a password before relying on this day to day.

Only `esp32s3_waveshare_lcd` has this wired up so far (it's the only board with Phase 0 done). The other boards need their own Phase 0 migration first (see the table above), then the same `ArduinoOTA` + `_ota` env pattern.

### Phase 2 — HTTP OTA via the proxy server (recommended for production)

This fits naturally into the existing architecture: the Pi already serves HTTP, already auto-deploys from GitHub, and already knows the firmware version.

**Concept:**

1. A CI step (GitHub Actions) builds the firmware binaries for all environments and commits them to the repo under `firmware/bin/`.
2. The auto-deploy script on the Pi pulls the new binaries along with everything else.
3. The proxy exposes a version endpoint per board: `GET /firmware/<env>/version` returning the current version string.
4. Each device checks this endpoint periodically (e.g. on boot + every 24 h). If the version differs from `APP_VERSION`, it fetches the binary from `GET /firmware/<env>/bin` and applies it using the ESP32 `Update` library.
5. The device reboots into the new firmware.

**Rollback safety:** ESP32 OTA uses two flash partitions (see Phase 0). If the new firmware crashes on first boot, the bootloader automatically falls back to the previous version — but only once Phase 0 has actually given the board a second slot.

**Status: implemented and verified on hardware for `esp32s3_waveshare_lcd`** (firmware v2.10/v2.11; server v1.19). The two proxy routes, and `checkForFirmwareUpdate()` in `main.cpp`, both work as designed — confirmed with a real download → `Update` → `ESP.restart()` → reboot → reconnect cycle, not just a successful HTTP transfer. What's not built yet: the GitHub Actions CI step (binaries are still hand-copied after tagging a release) and the other boards' Phase 0 migration (see the table above) — this is proven on one board, not fleet-wide.

**Incident 2026-08-06 — reboot loop, and the fix.** During live verification, a test intentionally relabeled the server's served version without changing the actual binary (to trigger the download path without needing a real second build). The device correctly detected the mismatch, downloaded, applied, and rebooted — but since the "new" binary was actually identical, `APP_VERSION` came back unchanged, so the *next* boot saw the exact same mismatch and repeated the cycle. Because this check ran in `setup()` **before** the dashboard ever rendered, the device reboot-looped indefinitely without ever showing the app, and had to be recovered with a USB reflash. Root cause: the check had no memory of "I already tried this exact offer and it didn't resolve." Fixed by persisting the last-attempted server version string in NVS (`Preferences`, key `lastOffer`) — a repeat of the *identical* offer is now skipped rather than retried, while any offer that actually changes still triggers normally. The boot-time call (re-enabled after this fix) also now runs *after* WiFi connects and the update-info splash renders, not blind at the very start of `setup()`.

**What this requires (remaining):**
- A GitHub Actions workflow that runs `pio run` for each environment and saves the `.bin` outputs, instead of the current hand-copy-after-tagging step.
- Phase 0 + this same wiring for the other boards (see the Phase 0 table above) — currently `esp32s3_waveshare_lcd` only.
- `APP_VERSION` is already `git describe`-derived (see the versioning memory) — CI needs to build from the same tagged commits this repo cuts, so the served version string matches what a real device would report.

### Update provenance — timestamp + method on the boot splash

`esp32s3_waveshare_lcd` only. The boot splash now shows a fifth line, e.g. `Updated 2026-08-06 19:04 UTC (OTA)`, so a glance at the screen answers "when did this last get flashed, and how" without needing serial access.

- **Method** (`OTA` vs `USB`): there's no code path for a USB reflash to leave a marker before rebooting — esptool writes flash directly. So `OTA` is recorded explicitly (an NVS flag, `pendingOta`, set just before the reboot in both `checkForFirmwareUpdate()` and `ArduinoOTA`'s `onEnd` hook) and `USB` is simply the default whenever that flag is absent.
- **Timestamp**: real wall-clock time needs NTP (`configTime()` + `getLocalTime()`, `pool.ntp.org` / `time.nist.gov`, UTC, 5 s timeout) — this device has no RTC. Best-effort: if NTP fails (no internet reachable from the WiFi network, not just LAN-to-the-Pi), the previously-recorded timestamp is left alone rather than blocking boot or showing garbage.
- **Detection**: on every boot, `recordFirmwareUpdateInfo()` compares `APP_VERSION` against the last value it recorded (NVS key `ver`). A change means this is the first boot on newly-flashed content, so it records "now" + the method and moves on; no change means an ordinary reboot, and it just re-reports whatever was recorded last time.
- All of this lives in NVS, not the served `/firmware/.../version.txt` — it reflects what's *actually running on this specific device*, independent of what the proxy happens to be serving.

### Server-visible device firmware version

Every `/weather` request (all boards, not just S3) now sends `X-Device-Firmware: <APP_VERSION>` alongside the existing `X-Device-Name`/`X-Device-Id` headers. The proxy stores it in a new `fw_version` column on the `devices` table (idempotent `ALTER TABLE` migration, same pattern as the existing `user_agent` column) and shows it as a badge in the admin device list. Empty for any device that hasn't checked in since this header was added. See `netatmo-home-hub`'s revision history for the server-side half.

---

## Summary and suggested order of work

| Priority | Work | Benefit |
|---|---|---|
| 1 | WiFiManager captive portal | Eliminates USB cable for provisioning entirely |
| 2 | Partition table migration (Phase 0, ESP32 only) | Prerequisite one-time USB reflash — makes real (rollback-safe) OTA possible at all |
| 3 | Arduino OTA (same-network) | Eliminates cable for updates on home network |
| 4 | GitHub Actions firmware CI | Produces versioned binaries automatically |
| 5 | HTTP OTA via proxy | Enables remote updates from anywhere |

Steps 2 and 3 together make the ESP32 fleet cable-free for normal home use (after the one Phase-0 reflash). Steps 4 and 5 are needed for true remote deployment.
