const express = require('express');
const app = express();
app.use(express.json({ limit: '1mb' }));

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
    author: 'local',
    type: 'Need',
    text: 'Looking for a ladder to borrow for the weekend.',
    expires: Math.floor(Date.now() / 1000) + 3600 * 6,
  },
];
let nextId = 4;
let sessionToken = '';

const identity = {
  name: 'mssg ina bttl',
  icon: '📬',
  tagline: 'Take what you need • Share what you can',
  rules: 'Be local • Be kind • No spam',
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
  if (key === 'lavish.meerkat') {
    sessionToken = Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2);
    return res.send(sessionToken);
  }
  res.status(403).send('forbidden');
});

function checkToken(req) {
  const t = req.query.token;
  return t && t === sessionToken;
}

app.get('/admin/config', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.json(identity);
});

app.get('/admin/identity/set', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  if (req.query.name) identity.name = req.query.name;
  if (req.query.icon) identity.icon = req.query.icon;
  if (req.query.tagline !== undefined) identity.tagline = req.query.tagline;
  if (req.query.rules !== undefined) identity.rules = req.query.rules;
  if (req.query.footer !== undefined) identity.footer = req.query.footer;
  res.send('identity saved');
});

app.get('/admin/time', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('time set');
});

app.get('/admin/led/get', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.json(ledConfig);
});

app.get('/admin/led/set', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  Object.keys(req.query).forEach((k) => {
    if (k === 'enabled' || k === 'pulse' || k === 'activity') {
      ledConfig[k] = req.query[k] === '1' || req.query[k] === 'true';
    } else if (ledConfig.hasOwnProperty(k)) {
      ledConfig[k] = parseInt(req.query[k], 10);
    }
  });
  res.send('LED settings saved');
});

app.get('/admin/clear', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  messages = [];
  res.send('cleared');
});

app.get('/admin/delete/post', (req, res) => {
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
  messages = Array.isArray(req.body) ? req.body : [];
  res.send(`restored ${messages.length} messages`);
});

app.get('/admin/setkey', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('key updated — page will reload');
});

app.get('/admin/flush', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('flushed');
});

app.post('/admin/ota', (req, res) => {
  if (!checkToken(req)) return res.status(403).send('forbidden');
  res.send('UPDATE OK — rebooting');
});

// ── Start ───────────────────────────────────────────────────────────────────
app.listen(3000, () => {
  console.log('Mock API listening on :3000');
});
