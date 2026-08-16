// ── Gate ────────────────────────────────────────────────────────────────────
function tryLogin() {
  const val = document.getElementById("keyIn").value;
  if (!val) return;

  fetch("/admin/auth", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ key: val }),
  })
    .then((r) => {
      if (!r.ok) throw new Error("forbidden");
      return r.text();
    })
    .then((token) => {
      SESSION_TOKEN = token;
      document.getElementById("gate").style.display = "none";
      document.getElementById("panel").style.display = "block";
      const now = new Date();
      now.setSeconds(0, 0);
      document.getElementById("timeIn").value = now.toISOString().slice(0, 16);

      // --- UPDATED: Load all data via API calls ---
      loadIdentity(); // <--- This fetches the Name, Icon, Tagline, etc.
      loadLedValues();
      loadPostList();
    })
    .catch(() => {
      document.getElementById("gateErr").textContent = "Incorrect key.";
    });
}

// --- NEW: Function to fetch identity data from the ESP ---
async function loadIdentity() {
  try {
    // We use apiFetch because it handles the SESSION_TOKEN automatically
    const r = await apiFetch("/admin/config");
    const d = await r.json();

    // Map the JSON keys to your HTML input IDs
    document.getElementById("idName").value = d.name;
    document.getElementById("idIcon").value = d.icon;
    document.getElementById("idTagline").value = d.tagline;
    document.getElementById("idRules").value = d.rules;
    document.getElementById("idFooter").value = d.footer;
  } catch (e) {
    console.error("Failed to load identity", e);
  }
}

document.getElementById("keyIn").addEventListener("keydown", (e) => {
  if (e.key === "Enter") tryLogin();
});

// ── Helpers ──────────────────────────────────────────────────────────────────
function api(path) {
  return (
    path +
    (path.includes("?") ? "&" : "?") +
    "token=" +
    encodeURIComponent(SESSION_TOKEN)
  );
}

function apiFetch(url, options) {
  return fetch(url, options).then((r) => {
    if (r.status === 403) {
      SESSION_TOKEN = "";
      document.getElementById("panel").style.display = "none";
      document.getElementById("gate").style.display = "block";
      document.getElementById("gateErr").textContent =
        "Session expired. Please log in again.";
      document.getElementById("keyIn").value = "";
      throw new Error("session expired");
    }
    return r;
  });
}

function fb(id, msg) {
  const el = document.getElementById(id);
  el.textContent = msg;
  setTimeout(() => {
    el.textContent = "";
  }, 4000);
}

// ── Change admin key ──────────────────────────────────────────────────────────
function doSetKey() {
  const n = document.getElementById("newKey").value;
  const c = document.getElementById("newKeyConfirm").value;
  if (n.length < 4) {
    fb("keyFb", "✗ Key must be at least 4 characters");
    return;
  }
  if (n !== c) {
    fb("keyFb", "✗ Keys do not match");
    return;
  }
  apiFetch(api("/admin/setkey") + "&newkey=" + encodeURIComponent(n))
    .then((r) => r.text())
    .then((msg) => {
      fb("keyFb", "✓ " + msg);
      // Reload after short delay so the page re-fetches with the new injected key
      setTimeout(() => location.reload(), 1500);
    })
    .catch(() => fb("keyFb", "✗ Request failed"));
}

// ── Identity ──────────────────────────────────────────────────────────────────
function doIdentity() {
  const params = new URLSearchParams({
    name: document.getElementById("idName").value.trim(),
    icon: document.getElementById("idIcon").value.trim(),
    tagline: document.getElementById("idTagline").value.trim(),
    rules: document.getElementById("idRules").value.trim(),
    footer: document.getElementById("idFooter").value.trim(),
  });
  apiFetch(api("/admin/identity/set") + "&" + params.toString())
    .then((r) => r.text())
    .then((msg) => fb("idFb", "✓ " + msg))
    .catch(() => fb("idFb", "✗ Request failed"));
}

// ── Time ─────────────────────────────────────────────────────────────────────
function doTime() {
  const raw = document.getElementById("timeIn").value; // "2026-04-22T14:30"
  if (!raw) {
    fb("timeFb", "✗ Please pick a date and time");
    return;
  }
  const [date, time] = raw.split("T");
  const [y, m, d] = date.split("-");
  const formatted = d + m + y + "-" + time.replace(":", "");
  apiFetch(api("/admin/time") + "&time=" + formatted)
    .then((r) => r.text())
    .then((msg) => fb("timeFb", "✓ " + msg))
    .catch(() => fb("timeFb", "✗ Request failed"));
}

// ── LED ──────────────────────────────────────────────────────────────────────
function loadLedValues() {
  apiFetch(api("/admin/led/get"))
    .then((r) => r.json())
    .then((d) => {
      document.getElementById("ledDayBr").value = d.day_br;
      document.getElementById("ledDayBrVal").textContent = d.day_br;
      document.getElementById("ledNightBr").value = d.night_br;
      document.getElementById("ledNightBrVal").textContent = d.night_br;
      document.getElementById("ledDayStart").value = d.day_st;
      document.getElementById("ledNightStart").value = d.night_st;
      document.getElementById("ledPin").value = d.pin;
      document.getElementById("ledEnabled").checked = d.enabled;
      document.getElementById("ledPulse").checked = d.pulse;
      document.getElementById("ledActivity").checked = d.activity;
      updateLedToggles();
    })
    .catch(() => {});
}

function updateLedToggles() {
  const enabled = document.getElementById("ledEnabled").checked;
  const pulse = document.getElementById("ledPulse").checked;
  document.getElementById("ledPulse").disabled = !enabled;
  document.getElementById("ledActivity").disabled = !enabled || !pulse;
}

function doLed() {
  const params = new URLSearchParams({
    day_br: document.getElementById("ledDayBr").value,
    night_br: document.getElementById("ledNightBr").value,
    day_st: document.getElementById("ledDayStart").value,
    night_st: document.getElementById("ledNightStart").value,
    pin: document.getElementById("ledPin").value,
    enabled: document.getElementById("ledEnabled").checked ? "1" : "0",
    pulse: document.getElementById("ledPulse").checked ? "1" : "0",
    activity: document.getElementById("ledActivity").checked ? "1" : "0",
  });
  apiFetch(api("/admin/led/set") + "&" + params.toString())
    .then((r) => r.text())
    .then((msg) => fb("ledFb", "✓ " + msg))
    .catch(() => fb("ledFb", "✗ Request failed"));
}

// ── Board actions ─────────────────────────────────────────────────────────────
async function doAction(path, fbId, isDownload) {
  const r = await apiFetch(api(path));
  const txt = await r.text();
  if (isDownload) {
    const a = document.createElement("a");
    a.href = "data:application/json," + encodeURIComponent(txt);
    a.download = "community_hub_backup.json";
    a.click();
    fb(fbId, "✓ Download started");
  } else {
    fb(fbId, "✓ " + txt);
  }
}

function confirmClear() {
  if (!confirm("Delete ALL posts? This cannot be undone.")) return;
  apiFetch(api("/admin/clear"))
    .then((r) => r.text())
    .then((msg) => fb("backupFb", "✓ " + msg))
    .catch(() => fb("backupFb", "✗ Failed"));
}

// ── Manage Posts ──────────────────────────────────────────────────────────────
function esc(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function timeLeftShort(exp) {
  const left = exp - Math.floor(Date.now() / 1000);
  if (left <= 0) return "expired";
  if (left < 3600) return Math.floor(left / 60) + "m";
  if (left < 86400) return Math.floor(left / 3600) + "h";
  return Math.floor(left / 86400) + "d";
}

async function loadPostList() {
  const container = document.getElementById("postList");
  container.innerHTML = '<div class="post-empty">Loading…</div>';
  try {
    const r = await fetch("/messages");
    const data = await r.json();
    if (data.length === 0) {
      container.innerHTML = '<div class="post-empty">No active posts.</div>';
      return;
    }
    container.innerHTML =
      '<div class="post-list">' +
      data
        .map(
          (m) => `
<div class="post-row" id="pr-${m.id}">
  <div class="post-row-info">
    <div class="post-row-meta">${esc(m.type)} · ${esc(m.author)} · ${timeLeftShort(m.expires)} left</div>
    <div class="post-row-text">${esc(m.text)}</div>
  </div>
  <button class="btn danger" onclick="deletePost(${m.id})">Delete</button>
</div>`,
        )
        .join("") +
      "</div>";
  } catch (_) {
    container.innerHTML = '<div class="post-empty">Failed to load posts.</div>';
  }
}

async function deletePost(id) {
  if (!confirm("Delete this post?")) return;
  const r = await apiFetch(api("/admin/delete/post") + "&id=" + id);
  if (r.ok) {
    const row = document.getElementById("pr-" + id);
    if (row) row.remove();
    fb("postListFb", "✓ Post deleted");
  } else {
    fb("postListFb", "✗ Delete failed");
  }
}


// ── OTA ───────────────────────────────────────────────────────────────────────
async function doOTA() {
  const fileInput = document.getElementById("otaFile");
  const manifestInput = document.getElementById("otaManifest");
  if (!fileInput.files.length) {
    fb("otaFb", "✗ Please select a .bin file");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }
  if (!manifestInput.files.length) {
    fb("otaFb", "✗ Please select a manifest .json file");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }

  let manifest;
  try {
    manifest = JSON.parse(await manifestInput.files[0].text());
  } catch (e) {
    fb("otaFb", "✗ Manifest is not valid JSON");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }
  if (!manifest.version || !manifest.signature) {
    fb("otaFb", "✗ Manifest missing version or signature");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }

  const file = fileInput.files[0];
  if (!file.name.endsWith(".bin")) {
    fb("otaFb", "✗ File must be a .bin firmware file");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }
  if (!file.size || file.size <= 0) {
    fb("otaFb", "✗ Could not read firmware file size");
    document.getElementById("otaProgress").style.display = "block";
    return;
  }
  if (
    !confirm(
      "Upload " +
        file.name +
        " (version " +
        manifest.version +
        ") and reboot?\n\nDo not close this page until complete.",
    )
  )
    return;

  document.getElementById("otaProgress").style.display = "block";
  document.getElementById("otaBar").style.width = "0%";
  fb("otaFb", "Uploading…");

  const url =
    api("/admin/ota") +
    "&version=" +
    encodeURIComponent(manifest.version) +
    "&sig=" +
    encodeURIComponent(manifest.signature) +
    "&size=" +
    file.size;
  console.log("OTA URL:", url);

  const xhr = new XMLHttpRequest();
  xhr.open("POST", url);

  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      document.getElementById("otaBar").style.width = pct + "%";
      fb("otaFb", "Uploading… " + pct + "%");
    }
  };

  xhr.onload = () => {
    document.getElementById("otaBar").style.width = "100%";
    if (xhr.status === 200) {
      fb("otaFb", "✓ " + xhr.responseText + " — connection will drop shortly");
    } else {
      fb("otaFb", "✗ Upload failed: " + xhr.responseText);
    }
  };

  xhr.onerror = () => fb("otaFb", "✗ Connection lost — board may be rebooting");

  const formData = new FormData();
  formData.append("firmware", file);
  xhr.send(formData);
}

// ── Restore ───────────────────────────────────────────────────────────────────
function doRestore() {
  const body = document.getElementById("restoreIn").value.trim();
  if (!body) return;
  apiFetch(api("/admin/restore"), {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body,
  })
    .then((r) => r.text())
    .then((msg) => fb("restoreFb", "✓ " + msg))
    .catch(() => fb("restoreFb", "✗ Failed"));
}
