const express = require('express');
const crypto = require('crypto');
const app = express();
app.use(express.json({ limit: '1mb' }));

const ADMIN_KEY_MIN_LEN = 12;
const ADMIN_KEY_PBKDF2_ITERS = 10000;
const ADMIN_KEY_SALT_BYTES = 16;

// ── State ───────────────────────────────────────────────────────────────────
let messages = [
  {
    id: 1,
    author: 'neighbor',
    type: 'Notice',
    text: 'Welcome to the dev server! This is mock data for local frontend work.',
    expires: Math.floor(Date.now() / 1000) + 86400 * 3,
  },
  {
    id: 2,
    author: 'dev',
    type: 'Offer',
    text: 'Free tomatoes from the garden — pickup on Maple St.',
    expires: Math.floor(Date.now() / 1000) + 86400 * 2,
  },
  {
    id: 3,
    author: 'neighbor',
    type: 'Notice',
    text: 'Looking for a ladder to borrow for the weekend.',
    expires: Math.floor(Date.now() / 1000) + 3600 * 6,
  },
  {
    id: 4,
    author: 'neighbor',
    type: 'Notice',
    text: 'Welcome to the dev server! This is mock data for local frontend work.',
    expires: Math.floor(Date.now() / 1000) + 86400 * 3,
  },
  {
    id: 5,
    author: 'dev',
    type: 'Offer',
    text: 'Free tomatoes from the garden — pickup on Maple St.',
    expires: Math.floor(Date.now() / 1000) + 86400 * 2,
  },
  {
    id: 6,
    author: 'neighbor',
    type: 'Notice',
    text: 'Looking for a ladder to borrow for the weekend.',
    expires: Math.floor(Date.now() / 1000) + 3600 * 6,
  },
];
let nextId = 4;
let sessionToken = '';

// Stored admin key hash (mirrors the ESP32 backend).
let adminKeySalt = crypto.randomBytes(ADMIN_KEY_SALT_BYTES);
let adminKeyHash = crypto.pbkdf2Sync(
  'lavish.meerkat',
  adminKeySalt,
  ADMIN_KEY_PBKDF2_ITERS,
  32,
  'sha256'
);

function setAdminKey(key) {
  adminKeySalt = crypto.randomBytes(ADMIN_KEY_SALT_BYTES);
  adminKeyHash = crypto.pbkdf2Sync(
    key,
    adminKeySalt,
    ADMIN_KEY_PBKDF2_ITERS,
    32,
    'sha256'
  );
}

function verifyAdminKey(key) {
  const computed = crypto.pbkdf2Sync(
    key,
    adminKeySalt,
    ADMIN_KEY_PBKDF2_ITERS,
    32,
    'sha256'
  );
  return crypto.timingSafeEqual(computed, adminKeyHash);
}

function checkToken(req) {
  const auth = req.headers.authorization || '';
  const t = auth.startsWith('Bearer ') ? auth.slice(7) : '';
  return t && t === sessionToken;
}

const identity = {
  name: 'mssg ina bttl',
  icon: '📬',
  // tagline: 'Take what you need • Share what you can',
  // rules: 'Be local • Be kind • No spam',
  footer: 'Powered locally — no internet required',
};

const ledConfig = {
  day_br: 80,
  night_br: 20,
  day_st: 7,
  night_st: 20,
  pin: 4,
  enabled: true,
  pulse: true,
  activity: true,
};

function uptime() {
  const s = Math.floor(process.uptime());
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d > 0) return `↑ ${d}d ${h}h ${m}m`;
  if (h > 0) return `↑ ${h}h ${m}m`;
  return `↑ ${m}m`;
}

// ── Public endpoints ────────────────────────────────────────────────────────
app.get('/api/status', (req, res) => {
  res.json({ full: messages.length >= 200 });
});

app.get('/messages', (req, res) => {
  const now = Math.floor(Date.now() / 1000);
  res.json(messages.filter((m) => m.expires > now));
});

app.post('/post', (req, res) => {
  const { author, type, text, expiry } = req.body;
  if (!text || !text.trim()) return res.status(400).send('empty message');
  messages.push({
    id: nextId++,
    author: (author || 'neighbor').toString().slice(0, 24),
    type: ['Notice', 'Offer', 'Need', 'Event'].includes(type) ? type : 'Notice',
    text: text.toString().slice(0, 300),
    expires: Math.floor(Date.now() / 1000) + (expiry || 72) * 3600,
  });
  res.send('ok');
});

app.get('/info', (req, res) => {
  res.json({ ...identity, uptime: uptime() });
});

// ── Admin endpoints ─────────────────────────────────────────────────────────
app.post('/admin/auth', (req, res) => {
  const { key } = req.body;
  if (verifyAdminKey(key)) {
    sessionToken =
      Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2);
    return res.send(sessionToken);
  }
  res.status(403).send('forbidden');
});

app.get('/admin/identity/get', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.json(identity);
});

app.post('/admin/identity/set', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  const sanitize = (s, maxLen) =>
    String(s || '').replace(/[<>]/g, '').trim().slice(0, maxLen);
  const validateType = (t) =>
    ['Notice', 'Offer', 'Need', 'Event'].includes(t) ? t : 'Notice';
  if (req.query.name) identity.name = sanitize(req.query.name, 48);
  if (req.query.icon) identity.icon = sanitize(req.query.icon, 8);
  if (req.query.tagline !== undefined) identity.tagline = sanitize(req.query.tagline, 100);
  if (req.query.rules !== undefined) identity.rules = sanitize(req.query.rules, 100);
  if (req.query.footer !== undefined) identity.footer = sanitize(req.query.footer, 100);
  res.send('identity saved');
});

app.post('/admin/time', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('time set');
});

app.get('/admin/led/get', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.json(ledConfig);
});

const ALLOWED_LED_PINS = [0, 2, 4, 12, 13, 14, 15, 21, 22, 25, 26, 32, 33];

app.post('/admin/led/set', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  Object.keys(req.query).forEach((k) => {
    if (k === 'enabled' || k === 'pulse' || k === 'activity') {
      ledConfig[k] = req.query[k] === '1' || req.query[k] === 'true';
    } else if (k === 'pin') {
      const pin = parseInt(req.query[k], 10);
      if (ALLOWED_LED_PINS.includes(pin)) ledConfig[k] = pin;
    } else if (ledConfig.hasOwnProperty(k)) {
      ledConfig[k] = parseInt(req.query[k], 10);
    }
  });
  res.send('LED settings saved');
});

app.post('/admin/clear', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  messages = [];
  res.send('cleared');
});

app.post('/admin/delete/post', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  const id = parseInt(req.query.id, 10);
  messages = messages.filter((m) => m.id !== id);
  res.send('deleted');
});

app.get('/admin/backup', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.json(messages);
});

app.post('/admin/restore', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  const arr = Array.isArray(req.body) ? req.body : [];
  const now = Math.floor(Date.now() / 1000);
  const maxExpires = now + 86400 * 365;
  const sanitize = (s, maxLen) =>
    String(s || '').replace(/[<>]/g, '').trim().slice(0, maxLen);
  const validateType = (t) =>
    ['Notice', 'Offer', 'Need', 'Event'].includes(t) ? t : 'Notice';
  messages = arr
    .map((m) => ({
      ...m,
      author: sanitize(m.author || 'neighbor', 24) || 'neighbor',
      type: validateType(m.type),
      text: sanitize(m.text, 300),
      expires: Math.min(parseInt(m.expires, 10) || 0, maxExpires),
    }))
    .filter((m) => m.text.length > 0)
    .slice(0, 200);
  res.send(`restored ${messages.length} messages`);
});

app.post('/admin/setkey', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  const { newkey } = req.query;
  if (!newkey || newkey.length < ADMIN_KEY_MIN_LEN) {
    return res.status(400).send('key must be at least 12 characters');
  }
  setAdminKey(newkey);
  sessionToken = '';
  res.send('key updated — log in again');
});

app.post('/admin/flush', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('flushed');
});

app.post('/admin/logout', (req, res) => {
  sessionToken = '';
  res.send('logged out');
});

app.post('/admin/ota', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('UPDATE OK — rebooting');
});


// ── Start ───────────────────────────────────────────────────────────────────
app.listen(3000, () => {
  console.log('Mock API listening on :3000');
});
