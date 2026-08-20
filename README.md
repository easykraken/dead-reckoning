# Dead Reckoning — adapting the `mssg ina bttl` codebase

## What the codebase is today

A neighborhood message board ("mssg ina bttl") on the Adafruit Feather ESP32 V2: it
raises its own WPA2 AP (password published in the SSID), wildcard-DNSes every hostname
to itself for captive-portal popup, serves a post/read web UI from LittleFS, and has a
hardened admin panel (PBKDF2-hashed key, 5-min bearer tokens, lockout) with signed,
button-gated OTA firmware updates. Docker mock-api + nginx mirror the device for local
frontend development.

## Evaluation: fit for Dead Reckoning

The codebase splits cleanly into **chassis** and **domain**.

**Chassis — keep, it's most of the work and it's already good:**
- AP + captive portal + DNS wildcard (`src/main.cpp` setup ~L1577–1614, probe handlers
  L1664–1705, `onNotFound` redirect L1742) — exactly the discovery mechanic we want.
- LittleFS static serving (`streamFile` pattern, L914–936, asset routes L1721–1739).
- Admin auth stack (PBKDF2 key storage, session tokens, constant-time compare,
  brute-force lockout — L139–347, L875–885, L938–1003). Fleet maintenance in public
  cafés needs this.
- Signed OTA with physical-button gate + anti-rollback (L349–473, L1358–1547) and
  `scripts/ota-tool.py`. Five devices living in cafés for months *must* be updatable
  without USB. This is the crown jewel of the existing code.
- Docker dev loop (`docker-compose.yml`, nginx + mock-api) — adapt, don't delete.
- `platformio.ini` — unchanged, same board.

**Domain — delete, ~100% of it:**
- Message model, storage, expiry, eviction (L630–783), per-IP rate limiting
  (L648–693), handlers `/messages` `/post` `/api/status` (L1005–1075, L1650–1662),
  admin backup/restore/clear/delete-post (L1190–1262, L1313–1356).
- `data/frontend.html` post form + board UI (all 340 lines), message CSS in
  `data/styles.css`.
- Mock message endpoints in `docker/mock-api/index.js` + nginx proxies for
  `/messages` `/post`.
- Time subsystem (L544–607) — only existed for message expiry; LED day/night was
  never ported to the Feather anyway. Keep a minimal heartbeat blink; drop persisted
  LED config + endpoints (L475–542, L1132–1188).

**Missing — the entire Dead Reckoning layer:**
- Station identity + fragment content model
- Player-facing pages (fragment landing, map/checklist)
- Stamp-word progress, save-and-carry affordances, download-all set

Net: this is a strong starting point — roughly the hardest 60% (networking, security,
OTA, dev loop) carries over intact; everything deleted makes the firmware *smaller*.

## Confirmed decisions (from design convo + this session)

- Phone-only first; e-ink FeatherWing deferred to a later phase.
- Every device carries **all** fragment assets; unlock is client-side honor-system via
  per-station stamp words. Enables download-all and the final assembled image from any
  station.
- Text narration, not audio. Still images everywhere; one moving moment (a short
  looping MP4) at **the morning** only.
- Fragment set: `the jetty`, `below`, `the morning`, `the museum`, `the future`,
  `her face` (+ `the return` finale, device-less, out of firmware scope).
- Finale page (public HTTPS site) is a separate static-site project — not this repo.

## Adaptation plan

### Phase 1 — Strip to chassis (firmware compiles, nothing player-facing yet)

`src/main.cpp`:
- Delete: `Message` struct/array, `saveMessages`/`loadMessages`/`addMessage`,
  rate-limit table + `checkPostRateLimit`, `handlePost`/`handleMessages`/`/api/status`,
  admin backup/restore/clear/delete-post handlers, time subsystem
  (`nowSecs`/`setTimeFromString`/`saveTime`/`loadTime`/`currentHour`), LED config
  persistence + LED admin handlers, `/admin/time`, related routes in `setup()` and
  dirty-save logic in `loop()`.
- Keep: Config namespace (trimmed), admin auth + OTA unchanged, captive portal,
  `handleRoot` static-serving pattern, uptime, heartbeat blink.
- Repurpose the identity system (`id_name/tagline/...`, `save/loadIdentityConfig`,
  L87–137) into **station config**: load `/station.json` at boot.

`data/`: delete `frontend.html` + message CSS; strip `admin.html`/`admin.js`/
`admin.css` of message, LED, and time sections (keep auth, identity→station, setkey,
OTA).

`docker/mock-api/index.js`: delete message state + endpoints; keep auth/OTA mocks.
`docker/nginx/default.conf`: drop `/messages` `/post` proxies.

### Phase 2 — Station content model

- `data/station.json` (per-device, set at deploy): `{ "station": "her-face" }`.
- `data/fragments.json` (identical on all devices): ordered manifest of all six
  fragments — id, title, image path, caption text, stamp word, station id, media type
  (`still` | `loop-video` for the morning).
- `data/frags/*.jpg` — all fragment images on every device (target ≤ ~250 KB each,
  ~1.5 MB total; verify against LittleFS partition size — if tight, add a custom
  partition table in `platformio.ini`, e.g. reduced app + larger LittleFS).
- Firmware: `GET /station` → this device's station.json (public; drives the landing
  page). Admin panel gains a **station picker** (dropdown → POST → writes
  `/station.json`), so every device ships with the identical LittleFS image and is
  assigned its station in the field. OTA replaces firmware only, so content and
  station assignment survive updates.
- Mock-api: serve `station.json` + `fragments.json` from `data/` so the docker loop
  keeps working.

### Phase 3 — Player pages (the actual piece)

Two pages, shared stylesheet, no frameworks (matches existing vanilla-JS style):

- `data/index.html` — **station landing**. Reads `/station`, pulls this fragment from
  `fragments.json`, renders: the still (or the morning's looping muted MP4), caption
  in the flat documentary voice, an explicit "keep this photograph" save affordance
  (long-press → Save to Photos; image served with download-friendly headers), and the
  station's **stamp word** reveal. First-visit overlay states the contract: *save
  every image you find — you will need them at the water.* Link to the map.
- `data/map.html` — **the manifest**. Checklist of all six fragments (poetic names,
  found/missing), stamp-word entry (honor system, persisted in localStorage),
  download-all set for unlocked fragments, and — when complete — the reveal of the
  final location with a Google Maps deep link to the ferry landing + stated yard
  access hours. Works identically on every station.
- localStorage note: captive-portal mini-browsers don't share storage with the real
  browser, and identical IP (`10.0.0.10`) on all stations keeps the origin stable when
  the player uses one real browser — stamp words are the robust path either way;
  re-entry is cheap if storage is lost.
- SSID scheme: unique per station, ≤32 bytes, keeping the published-password WPA2
  model, e.g. `DR · HER FACE (key: jetee)`. Fragment name in the SSID is the
  discovery hook.

### Phase 4 — Fleet config, docs, verification

- Config: new admin key, per-station SSIDs, run `scripts/ota-tool.py generate`
  (replace placeholder key in `src/ota_public_key.h`).
- README rewrite for Dead Reckoning (deploy + station-assignment + OTA runbook).
- Verify: `docker compose up --build` for page iteration → `pio run` compiles →
  flash one device, join from iOS *and* Android (captive portal behavior differs),
  stamp + checklist flow across two devices, signed-OTA round trip.

## Explicitly out of scope (this pass)

- E-ink FeatherWing display (later phase).
- Public finale web page / geofencing (separate static site).
- Visit counters / analytics.

## Order of work

1. Phase 1 strip (`src/main.cpp`, `data/`, mock) — skeleton compiles.
2. Phase 2 content model (`/station`, station picker, manifest).
3. Phase 3 pages (`index.html`, `map.html`, styles).
4. Phase 4 config/docs/verification.
