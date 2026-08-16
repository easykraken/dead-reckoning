# mssg ina bttl

mssg ina bttl is a hyper-local, self-contained, internet-free neighborhood message board running on an ESP32. This specific version runs on an Adafruit HUZZAH32 v2, the ESP32-based Feather board. It creates its own local WiFi network, serves a responsive web interface, and persists all messages and settings to internal flash.

Pair the Feather with a LiPoly battery, drop them into a cute container and then your pocket, then go on with yo hyper-local self.

<img src="./media/mssg-pair.jpg" style="height:560px;" alt="mssg ina bttl in a tin with a key">


## Forked with thanks from SonicDH

Community Hub
- https://github.com/SonicDH/Community-Hub


With an excellent breakdown by the author, Victor Frost:
- https://www.heyvictorfrost.com/workshop/Community_hub_V1


## Features

- **Zero-Internet Operation** – Runs entirely on a local Access Point. No cloud, no external dependencies.
- **Message Board** – Auto-expiration & smart storage management.
- **Admin Panel** – Full configuration interface for board identity, time, LED behavior, backup/restore, and signed firmware updates.
- **Session-Based Auth** – Secure admin access using short-lived tokens (30-minute lifetime). Admin key is never transmitted to the browser.
- **Persistent Storage** – All settings and messages survive reboots via LittleFS.
- **Smart LED Indicator** – Day/night brightness, sine-wave pulsing, and faster pulsing on recent activity.
- **Captive Portal Support** – Auto-redirects iOS, Android, Windows, and Firefox connectivity probes to the main board.

---

## Hardware & Software Requirements

| Component | Requirement |
|-----------|-------------|
| **MCU** | ESP32 (Adafruit Feather V2) |
| **Storage** | Internal Flash (LittleFS) |
| **IDE** | VS Code with PlatformIO Extension |
| **Board Package** | `espressif32` via PlatformIO |
| **Partition Scheme** | `Default 4MB` |
| **Library** | `ArduinoJson` (v7+) |

---

## Hardware & Software Used

The setup used in this specific build:

- [Adafruit ESP32 Feather V2](https://learn.adafruit.com/adafruit-esp32-feather-v2/overview)
- [Lithium Ion Polymer Battery - 3.7v 500mAh](https://www.adafruit.com/product/1578)

<img src="./media/readme-featherV2.jpg" style="width:420px; float:left;" alt="adafruit esp32 feather v2">
<img src="./media/readme-lipoly-battery.jpg" style="width:420px;" alt="lipoly battery">

---

## Installation & Setup

1. **Prerequisites**
   - Install **VS Code** and the **PlatformIO IDE** extension.
   - Ensure your ESP32 Feather V2 is connected via USB.

2. **Project Configuration**
   PlatformIO manages dependencies and board support via `platformio.ini`.
   Ensure your configuration file contains the following:

   ```ini
   [env:adafruit_feather_esp32_v2]
   platform = espressif32
   board = adafruit_feather_esp32_v2
   framework = arduino
   monitor_speed = 115200
   upload_speed = 115200
   board_build.filesystem = littlefs

   ; Dependencies
   lib_deps =
     bblanchon/ArduinoJson@^7.4.3
   ```

3. **Upload LittleFS Files**
   The backend expects frontend assets in LittleFS:
   - `/frontend.html`, `/styles.css`
   - `/admin.html`, `/admin.css`, `/admin.js`
   
   Place these files in the `data/` folder at the root of your project. Then run:
   ```bash
   pio run --target uploadfs
   ```
   *(Or click the "Upload Filesystem Image" button in the VS Code PlatformIO toolbar)*

4. **Build & Upload Sketch**
   Build and upload the firmware using:
   ```bash
   pio run --target upload
   ```
   *(Or click the "Upload" button in the VS Code PlatformIO toolbar)*

   Day-to-day firmware updates can also be done over-the-air; see the OTA Updates section below.

5. **Power On & Connect**
   The device will boot and create a WiFi network. Connect to it and open any browser to `http://10.0.0.10` (or just open a browser to trigger captive portal).
   Use `pio device monitor` to view serial output for debugging.

---

## Local Development

You can run the frontend and a mock API locally with Docker — no ESP32 required. This is useful for iterating on HTML, CSS, or JS without re-flashing the board.

A `docker-compose.yml` and mock API are included in the repo.

```bash
docker compose up --build
```

Then open:
- **Board:** `http://localhost:8888`
- **Admin panel:** `http://localhost:8888/admin`

The default admin key for the mock server is `lavish.meerkat`.

### What it runs

| Service | Purpose |
|---------|---------|
| `nginx` | Serves static files from `data/` (the same files uploaded to LittleFS) |
| `mock-api` | Express server that mimics the ESP32 backend endpoints |

### Mapped endpoints

The nginx container proxies API calls to the mock server, so the frontend works exactly as it does on the device:

| Endpoint | Mock behavior |
|----------|---------------|
| `GET /api/status` | Returns `{ "full": false }` |
| `GET /messages` | Returns seeded sample posts |
| `POST /post` | Adds a post to in-memory storage |
| `GET /info` | Returns board identity & uptime |
| `POST /admin/auth` | Issues a session token if key matches |
| `GET /admin/config` | Returns current identity settings |
| `GET /admin/led/get` | Returns LED configuration |
| `GET /admin/led/set` | Updates LED settings in memory |
| `GET /admin/clear` | Wipes all mock posts |
| `GET /admin/delete/post` | Removes a post by ID |
| `GET /admin/backup` | Downloads messages as JSON |
| `POST /admin/restore` | Restores messages from JSON |
| `POST /admin/ota` | Accepts any token-valid POST and returns success |

All mock data lives in memory — restart the container to reset to seed data.

### Changing the port

If `8888` is taken, edit the `ports` mapping in `docker-compose.yml`:

```yaml
ports:
  - "8888:80"
```

---

## OTA Updates

OTA is supported, but it requires **three independent checks** to reduce the chance of a remote takeover:

1. **A physical button press** on the board.
2. **A valid ECDSA P-256 signature** over the firmware binary.
3. A **version number higher** than the last accepted OTA version (anti-rollback).

> **⚠️ Important:** The public key in `src/ota_public_key.h` is a compile-time placeholder. Run `python scripts/ota-tool.py generate` and re-flash before deploying, or the device will not accept your signed firmware. The private key (`ota_private.pem`) must stay offline and **never** be committed.

> OTA signing prevents malicious firmware from being flashed, but it does **not** encrypt the upload. The `.bin` and `.manifest.json` still travel in plaintext over the open AP, so treat the private key like a secret.

### Workflow

#### 1. Generate a keypair (once)

```bash
python scripts/ota-tool.py generate
```

This creates `ota_private.pem` (keep it secret) and `ota_public.pem`, and writes the public key to `src/ota_public_key.h`. Rebuild and re-flash the board so it has your public key.

#### 2. Build a new firmware

```bash
pio run --target upload
```

This produces `.pio/build/adafruit_feather_esp32_v2/firmware.bin`.

#### 3. Sign the firmware

```bash
python scripts/ota-tool.py sign \
  .pio/build/adafruit_feather_esp32_v2/firmware.bin \
  --version 2
```

The script:

- Computes the SHA-256 hash of the binary.
- Builds the string `version|hash`.
- Signs that string with your private key using ECDSA P-256.
- Writes `firmware.bin.manifest.json` containing the version, hash, and base64 signature.

Including the version in the signature lets the device enforce anti-rollback. Once it accepts version 2, it will reject any update signed as version 1 or lower. The last accepted version is stored in `/otaversion.json` on LittleFS.

#### 4. Enable OTA on the device

Press the OTA enable button (default **SW38 / GPIO 38**, the onboard button). This opens a 5-minute window during which `/admin/ota` will accept uploads. A remote attacker with a stolen token still cannot flash firmware without physical access to that button.

#### 5. Upload from the admin panel

1. Open `http://10.0.0.10/admin` and log in.
2. Scroll to **Firmware Update (OTA)**.
3. Select the `.bin` file and the `.manifest.json` file.
4. Click **Upload & Reboot**.

The browser sends `version`, `sig`, and `size` as query parameters and the binary as the multipart upload body.

#### 6. What the device does

During the upload the device:

1. Checks the admin token and the 5-minute OTA window.
2. Rejects the claimed size if it is zero or over the max size cap.
3. Streams the binary into the ESP32 OTA flash partition while computing its SHA-256.
4. After the upload finishes:
   - Verifies the received size matches the claimed size.
   - Finishes the SHA-256 hash.
   - Reconstructs `version|hash` and verifies the ECDSA signature against the public key in `src/ota_public_key.h`.
   - Checks that the version is greater than the last accepted version.

If **any** check fails, the device aborts the update and the old firmware stays active. If everything passes, it finalizes the update, saves the new accepted version, and reboots into the new firmware.

### Button wiring

By default `OTA_BUTTON_PIN` is set to **GPIO 38** (`BUTTON` / SW38), which already has an onboard pull-up. Just press SW38 once to enable OTA mode for 5 minutes. Change `Config::OTA_BUTTON_PIN` in `src/main.cpp` if you want to use a different pin.

### Threats this stops

| Threat | Stopped by |
|---|---|
| Remote attacker with a stolen token flashes bad firmware | Physical button + signature verification |
| Attacker replays an old signed firmware | Version anti-rollback |
| Attacker tampers with the `.bin` in transit | Signature no longer matches hash |
| Attacker tampers with the manifest | Signature no longer matches |

### What it does not stop

- The upload is **not encrypted**. Anyone within WiFi range can see the firmware bytes. Signing prevents tampering, not eavesdropping.
- Physical access to the device can dump the flash, including the public key and stored data. It still cannot produce valid signed firmware without the private key.

---

## Configuration & Usage

### Default Network Settings
| Setting | Value |
|---------|-------|
| **SSID** | `mssg ina bttl` |
| **Password** | Open network (none) |
| **Channel** | 6 |
| **Max Clients** | 8 |

### Admin Access
1. Navigate to `http://<device-ip>/admin`
2. Enter the admin key. **Default:** `lavish.meerkat` *(change immediately via the admin panel or by editing `Config::ADMIN_KEY`)*
3. You'll receive a session token valid for 30 minutes.

### Setting the Time
The device tracks time internally but drifts without NTP. Set it manually via the admin panel using the format:
```
DDMMYYYY-HHMM
```
Example: `15042024-1430` → April 15, 2024 at 14:30

### LED Behavior (Pin 4 by default)

This 👇 is taken from the original repo. I haven't ported this to the Feather as of yet.

- **Day Mode:** Higher brightness, slow pulse
- **Night Mode:** Lower brightness, slow pulse
- **Activity Mode:** Faster pulse for 3 hours after any new post
- All thresholds, pin, and toggles are adjustable in the admin panel.

---

## API & Endpoints

### Public
| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | Main board interface |
| `GET` | `/info` | Board identity & uptime (JSON) |
| `GET` | `/messages` | Active messages (JSON) |
| `POST` | `/post` | Submit new message (JSON body) |
| `GET` | `/api/status` | Board capacity status (`{ "full": true/false }`) |

### Admin (Requires valid session token)
| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/admin/auth` | Authenticate & receive session token |
| `GET` | `/admin/identity/get` | Get board identity settings |
| `POST` | `/admin/identity/set` | Update board identity |
| `POST` | `/admin/time` | Set internal clock |
| `GET/POST` | `/admin/led/get` & `/admin/led/set` | LED configuration |
| `GET` | `/admin/backup` | Download messages JSON |
| `POST` | `/admin/restore` | Restore messages from JSON |
| `POST` | `/admin/setkey` | Change admin key |
| `POST` | `/admin/flush` | Force-save all pending data |
| `POST` | `/admin/ota` | Signed firmware update (requires button + signature) |
| `POST` | `/admin/clear` | Wipe all messages |
| `POST` | `/admin/delete/post` | Remove specific message by `id` |

---

## Security Notes

- **Local-Only Network:** The device never connects to the internet. All traffic stays within the AP.
- **Session Tokens:** Admin key is only used once to generate a 30-minute token. Subsequent requests use `?token=...`.
- **Input Sanitization:** All user text is stripped of `< >` characters and trimmed. Max lengths enforced.
- **Type Validation:** Only `Notice`, `Offer`, `Need`, `Event` are accepted. Others default to `Notice`.
- **Change Default Key:** The hardcoded fallback is `lavish.meerkat`. Update it via the admin panel or source code before deployment.
- **Signed OTA:** Firmware updates require an ECDSA signature, a higher version number, and a physical button press. Replace the sample public key in `src/ota_public_key.h` with your own key before deploying.

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `File not found` on boot | LittleFS wasn't flashed. Re-run the LittleFS Data Upload tool. |
| Captive portal doesn't trigger | Samsung/Android devices sometimes ignore redirects. Try opening `http://10.0.0.10` directly. |
| Board says "full" but has space | 200 message limit is hard-coded. Expired posts must be cleared or the board must be flushed. |
| LED not working | Verify pin 4 is unoccupied. Change `led_pin` in admin panel if using a different GPIO. |
| OTA says "ota disabled" | Press the OTA enable button (default SW38 / GPIO 38) to open the 5-minute OTA window. |
| OTA says "signature verify failed" | Make sure `src/ota_public_key.h` contains the public key matching your `ota_private.pem`, and that the manifest version is higher than the last accepted version. |
| OTA says "downgrade rejected" | Increase the `--version` number when signing; the device only accepts increasing versions. |
| Time drifts significantly | Set time manually via admin panel. Consider adding an RTC module if long-term accuracy is needed. |

---

> 💡 **Tip:** To customize the AP name, admin key, or default settings, edit the `Config` namespace at the top of `main.cpp` before your first upload. All runtime changes are persisted automatically.