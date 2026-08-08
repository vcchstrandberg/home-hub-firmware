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

Investigated 2026-08-06 by inspecting the actual partition CSVs and board flash sizes rather than assuming — including, for `esp32cam`, an assumption that turned out to be wrong (see below). **Never trust "no `board_build.partitions` override" to mean a particular default without decoding the actual build output** (`gen_esp32part.py path/to/partitions.bin`, bundled with the arduino-esp32 framework package) — two boards on the exact same `platform = espressif32` declaration with no override resolved to *different* default schemes.

- **`huge_app.csv`** (originally used by `esp32c6_waveshare_lcd`, `esp32c6_zero_ili9341`, `esp32s3_waveshare_lcd`) defines an `otadata` partition and one `app0` slot typed `ota_0` — but there is **no second app slot**. `Update.h` could technically overwrite `app0` in place, but there's no A/B fallback: a bad image bricks the device until it's walked back to a USB port. Not a foundation to build real OTA on.
- **`esp32cam` was wrongly assumed OTA-shaped.** Despite no `board_build.partitions` override — same as `esp32dev`, which genuinely does land on the two-slot `default.csv` — decoding `esp32cam`'s actual build output found a single 3 MB `ota_0` slot with no `ota_1`, the same no-rollback shape as `huge_app.csv`. Not derivable from the board JSON; only the decoded partition table caught it. Fixed.
- Flash headroom: the C6 boards' actual chips are **8 MB** (confirmed via `esptool flash-id` directly on hardware 2026-08-06, not just Waveshare's spec page — chip is `ESP32-C6FH8`), and the S3 board is **16 MB**. `esp32cam`/`esp32dev` are the conventional 4 MB (not hardware-verified for `esp32dev` since it wasn't connected — a 4 MB-total table is safe regardless, it just leaves extra room unused on a bigger chip).
- **Slots got tight once OTA code was added.** The `default.csv` 1.25 MB slots (`esp32c6_zero`, and previously assumed for `esp32cam`/`esp32dev`) were fine for the original OLED firmware, but `ArduinoOTA` + `Update` + `Preferences` + NTP pushed the binary to **95.3% full** on `esp32c6_zero` — one real feature away from not fitting. Widened every small-slot board to `ota_nofs_4MB.csv` (two 1.9375 MB slots, no spiffs — none of these boards use a filesystem), dropping usage back to ~60%.
- **A partition table change can only be applied via a normal USB flash** — PlatformIO always writes the full image (bootloader + partition table + app) on upload, so this rides along with one ordinary `--target upload`, but it can't be pushed over OTA itself (the new layout has to exist before OTA can use it). This is the one unavoidable cable trip per board; after it, routine updates no longer need one.

**Per-board status:**

| Target | Scheme | Slot size | Status |
|---|---|---|---|
| `esp32s3_waveshare_lcd` | `default_16MB.csv` (built-in) | 6.25 MB × 2 | Done — v2.8, verified on hardware |
| `esp32c6_waveshare_lcd` | `partitions_c6_8mb_ota.csv` (custom, repo root) | 3 MB × 2 | Done — see below |
| `esp32c6_zero_ili9341` | `partitions_c6_8mb_ota.csv` (same custom file) | 3 MB × 2 | Done — see below |
| `esp32c6_zero` | `ota_nofs_4MB.csv` (built-in) | 1.9375 MB × 2 | Done in code; not yet flashed (board not connected) |
| `esp32cam` | `ota_nofs_4MB.csv` (built-in) | 1.9375 MB × 2 | Done in code; not yet flashed (board not connected) |
| `esp32dev` | `ota_nofs_4MB.csv` (built-in) | 1.9375 MB × 2 | Done in code; not yet flashed (board not connected) |

The custom `partitions_c6_8mb_ota.csv` exists because no built-in arduino-esp32 scheme fits an 8 MB chip with two ~3 MB slots — `default_16MB.csv`'s slots don't fit, and the small schemes waste most of the chip.

### Phase 1 — Arduino OTA (same network, no cable)

ESP32 has built-in OTA support. Once enabled, `pio run -e <env>_ota --target upload --upload-port <device-ip>` pushes firmware over WiFi — no cable needed. Requires Phase 0 (a two-slot partition table) to actually be safe.

**Limitation:** requires the developer's machine to be on the same local network as the device. Good enough for home use, not for remote deployment.

**Implementation note:** the codebase has a few synchronous busy-wait loops (boot splash, `showLocale()`) that don't pump anything else while blocked. `ArduinoOTA.handle()` in `loop()` won't run during those few seconds — acceptable (OTA just isn't reachable mid-splash/mid-locale-cycle), but worth calling out rather than discovering it as a bug later.

**Env pattern:** `upload_protocol = espota` is set on a *separate* PlatformIO env per board (e.g. `esp32c6_waveshare_lcd_ota`, via `extends = env:esp32c6_waveshare_lcd`) rather than on the base env. Setting it on the base env would make plain `-e <env> --target upload` try to reach the device over WiFi by default — breaking the first install (the ArduinoOTA listener isn't running yet) and removing the USB fallback. PlatformIO Core 6.1.19's `extends` needs the `env:` prefix (`extends = env:esp32c6_waveshare_lcd`, not just the bare name) — the bare form silently drops the inherited `platform`/`board` and fails with `UndefinedEnvPlatformError`.

**Verified on hardware 2026-08-06** (`esp32s3_waveshare_lcd`, then the same pattern applied fleet-wide): flashed the ArduinoOTA-enabled build via USB once (required — the listener has to already be running before anything can reach it over WiFi), read the device's DHCP IP off serial, then pushed the same build again via the `_ota` env. Transfer completed, device rebooted on its own, and reconnected to WiFi and the hub normally. Currently unauthenticated (no `ArduinoOTA.setPassword()`) — fine for same-LAN verification, but add a password before relying on this day to day.

`OTA_ENV_NAME` in `main.cpp` (defined per board in the platform-selection block) drives both the ArduinoOTA hostname and the Phase 2 `/firmware/<env>/...` URL — a board only gets any OTA code compiled in when this macro is defined, which happens once its Phase 0 migration is done. Currently defined for all six ESP32 targets; absent for `uno_r4_wifi` (different chip family, out of scope).

### Phase 2 — HTTP OTA via the proxy server (recommended for production)

This fits naturally into the existing architecture: the Pi already serves HTTP, already auto-deploys from GitHub, and already knows the firmware version.

**Concept:**

1. A CI step (GitHub Actions) builds the firmware binaries for all environments and commits them to the repo under `firmware/bin/`.
2. The auto-deploy script on the Pi pulls the new binaries along with everything else.
3. The proxy exposes a version endpoint per board: `GET /firmware/<env>/version` returning the current version string.
4. Each device checks this endpoint periodically (e.g. on boot + every 24 h). If the version differs from `APP_VERSION`, it fetches the binary from `GET /firmware/<env>/bin` and applies it using the ESP32 `Update` library.
5. The device reboots into the new firmware.

**Rollback safety:** ESP32 OTA uses two flash partitions (see Phase 0). If the new firmware crashes on first boot, the bootloader automatically falls back to the previous version — but only once Phase 0 has actually given the board a second slot.

**Status: implemented and verified on hardware for `esp32s3_waveshare_lcd`** (firmware v2.10–v2.14; server v1.19–v1.20), then rolled out in code to the rest of the ESP32 fleet 2026-08-06 (`checkForFirmwareUpdate()`, `recordFirmwareUpdateInfo()`, `syncTimeFromNtp()` generalized from S3-only to any board with `OTA_ENV_NAME` defined). `esp32c6_waveshare_lcd` and `esp32c6_zero_ili9341` additionally verified on hardware the same day (USB flash of the OTA-enabled build; `esp32c6_zero`/`esp32cam`/`esp32dev` done in code only, not yet flashed since those boards weren't connected). What's not built yet: the GitHub Actions CI step (binaries are still hand-copied after tagging a release).

**Incident 2026-08-06 — reboot loop, and the fix.** During live verification, a test intentionally relabeled the server's served version without changing the actual binary (to trigger the download path without needing a real second build). The device correctly detected the mismatch, downloaded, applied, and rebooted — but since the "new" binary was actually identical, `APP_VERSION` came back unchanged, so the *next* boot saw the exact same mismatch and repeated the cycle. Because this check ran in `setup()` **before** the dashboard ever rendered, the device reboot-looped indefinitely without ever showing the app, and had to be recovered with a USB reflash. Root cause: the check had no memory of "I already tried this exact offer and it didn't resolve." Fixed by persisting the last-attempted server version string in NVS (`Preferences`, key `lastOffer`) — a repeat of the *identical* offer is now skipped rather than retried, while any offer that actually changes still triggers normally. The boot-time call also now runs *after* WiFi connects and the update-info splash renders, not blind at the very start of `setup()`. A second, related bug (v2.13) was the `pendingOta` NVS flag surviving across USB flashes (which don't touch NVS) and mis-attributing a later USB update as OTA — fixed by consuming the flag unconditionally on every boot instead of only when a version change is detected.

**What this requires (remaining):**
- A GitHub Actions workflow that runs `pio run` for each environment and saves the `.bin` outputs, instead of the current hand-copy-after-tagging step.
- Flashing + verifying `esp32c6_zero`, `esp32cam`, `esp32dev` once they're next connected (code is ready; Phase 0's USB reflash hasn't happened for them yet).
- `APP_VERSION` is already `git describe`-derived (see the versioning memory) — CI needs to build from the same tagged commits this repo cuts, so the served version string matches what a real device would report.

### Update provenance — timestamp + method on the boot splash

All ESP32 targets with `OTA_ENV_NAME` defined. Boards with real screen space (both LVGL boards, the ILI9341 TftUI board) show a 5th splash line, e.g. `Updated 2026-08-06 19:04 UTC (OTA)`. The 128×64 OLED boards (`esp32cam`, `esp32dev`, `esp32c6_zero`) have no room for a 5th line on top of the existing 4-line splash, so for them this only shows up as a `Serial.println("Updated: ...")` line — a deliberate scope trade-off given the display size, not an oversight.

- **Method** (`OTA` vs `USB`): there's no code path for a USB reflash to leave a marker before rebooting — esptool writes flash directly. So `OTA` is recorded explicitly (an NVS flag, `pendingOta`, set just before the reboot in both `checkForFirmwareUpdate()` and `ArduinoOTA`'s `onEnd` hook) and `USB` is simply the default whenever that flag is absent. The flag is consumed (read + cleared) unconditionally on every boot (see the v2.13 fix above) so it can never survive more than one boot cycle.
- **Timestamp**: real wall-clock time needs NTP (`configTime()` + `getLocalTime()`, `pool.ntp.org` / `time.nist.gov`, UTC, 5 s timeout) — none of these devices have an RTC. Best-effort: if NTP fails (no internet reachable from the WiFi network, not just LAN-to-the-Pi), the previously-recorded timestamp is left alone rather than blocking boot or showing garbage.
- **Detection**: on every boot, `recordFirmwareUpdateInfo()` compares `APP_VERSION` against the last value it recorded (NVS key `ver`). A change means this is the first boot on newly-flashed content, so it records "now" + the method and moves on; no change means an ordinary reboot, and it just re-reports whatever was recorded last time.
- All of this lives in NVS, not the served `/firmware/.../version.txt` — it reflects what's *actually running on this specific device*, independent of what the proxy happens to be serving.

### Server-visible device firmware version

Every `/weather` request (all boards, including the Uno R4) now sends `X-Device-Firmware: <APP_VERSION>` alongside the existing `X-Device-Name`/`X-Device-Id` headers — this part isn't OTA-gated, `APP_VERSION` already exists for every target via `scripts/version.py`. The proxy stores it in a new `fw_version` column on the `devices` table (idempotent `ALTER TABLE` migration, same pattern as the existing `user_agent` column) and shows it as a badge in the admin device list. Empty for any device that hasn't checked in since this header was added. See `netatmo-home-hub`'s revision history for the server-side half.

### OTA UX: fast propagation, install-progress splash, failure tracking

Shipped 2026-08-08 (firmware v2.20, server v1.22), all six OTA-enabled targets. Three independent enhancements on top of the working Phase 2 pull mechanism:

**Fast propagation.** Waiting up to 24 h (or a manual reboot) for a device to notice a new publish was the main remaining rough edge. Rather than add a genuine server→device push (a new listening service on every device, more complexity than the win justifies), the fix piggybacks on the `/weather` poll every device already makes every ~5 min: the device sends `X-Device-Env: <OTA_ENV_NAME>`, the server answers with a `latest_firmware` field (`_firmware_version_for()`, the same lookup `/firmware/<env>/version` already does), and `parseWeather()` calls `checkForFirmwareUpdate()` immediately on a mismatch instead of waiting for the periodic check. `checkForFirmwareUpdate()` doesn't trust this blindly — it re-fetches and re-compares the version itself, and the existing `lastOffer` NVS guard still applies — so this field is purely a "go check now" trigger, not a new code path.

**Install-progress splash.** Previously the screen gave no indication anything was happening between "check passed" and the reboot — worst case, several seconds of an apparently-frozen dashboard while a large binary downloaded. `checkForFirmwareUpdate()` now shows an interrupting "Installing update" screen (new `showOtaProgress()` in `LvglUI`/`TftUI`, a redrawn screen inline for the OLED boards) as soon as the `lastOffer` guard passes and a real attempt is starting — this fires whether the check was triggered at boot, by the periodic 24 h timer, or by the new fast-path.

**Failed-install tracking.** A download that starts (`Update.begin()` succeeds) but doesn't finish cleanly — a short `writeStream()` or a failed `Update.end()`/`isFinished()` — now persists `lastFailVer`/`lastFailReason` in NVS via `recordFirmwareUpdateFailure()`. Deliberately narrower than "any OTA failure": the earlier network-level failures (proxy unreachable, HTTP error on the version/bin fetch, even `Update.begin()` itself failing) are *not* persisted, since those are ordinary transient conditions the next periodic check will just retry — persisting on every blip would train the user to ignore a warning that's supposed to mean "this needs a human." The recorded failure shows up on the *next* boot (not immediately — a failed attempt doesn't reboot, the device just keeps running what it already had) as an extra red line on `showBootSplash()` for the two LVGL boards and the ILI9341 TftUI board, or a dedicated full-screen replacement ("OTA update failed … Connect via USB") on the three 128×64 OLED boards, which have no spare line for it and whose splash runs before WiFi connects (NVS doesn't need WiFi, so the check happens there rather than after, unlike the other boards' extra splash line). Cleared automatically the next time `recordFirmwareUpdateInfo()` sees `APP_VERSION` actually change — a successful OTA retry or a USB fix both clear it the same way, since either means the failure is no longer describing the currently-running code.

---

## Summary and suggested order of work

| Priority | Work | Benefit |
|---|---|---|
| 1 | WiFiManager captive portal | Eliminates USB cable for provisioning entirely |
| 2 | Partition table migration (Phase 0, ESP32 only) | Prerequisite one-time USB reflash — makes real (rollback-safe) OTA possible at all |
| 3 | Arduino OTA (same-network) | Eliminates cable for updates on home network |
| 4 | GitHub Actions firmware CI | Produces versioned binaries automatically |
| 5 | HTTP OTA via proxy | Enables remote updates from anywhere |

Steps 2 and 3 are done fleet-wide in code as of 2026-08-06 (verified on hardware for `esp32s3_waveshare_lcd`, `esp32c6_waveshare_lcd`, `esp32c6_zero_ili9341`; the rest await their next USB connection). Steps 4 and 5 are needed for true remote deployment.
