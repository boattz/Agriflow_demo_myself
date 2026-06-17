const express = require('express');
const cors    = require('cors');
const mysql   = require('mysql2/promise');
const os      = require('os');

const app  = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

// Serve only dashboard files (not .ino, .h, server.js etc.)
app.use(express.static(__dirname, {
  index: 'index.html',
  extensions: ['html'],
  setHeaders: (res, path) => {
    // Only allow specific file types
    const allowed = ['.html', '.css', '.js', '.json', '.png', '.jpg', '.svg', '.ico'];
    const ext = require('path').extname(path);
    if (!allowed.includes(ext)) {
      res.status(403).end();
    }
  }
}));

// ── TiDB Connection ───────────────────────────
const dbConfig = {
  host:     process.env.TIDB_HOST     || 'localhost',
  port:     process.env.TIDB_PORT     || 4000,
  user:     process.env.TIDB_USER     || 'root',
  password: process.env.TIDB_PASSWORD || '',
  database: process.env.TIDB_DATABASE || 'smart_sprinkler',
  ssl:      { rejectUnauthorized: true }
};

let db = null;

async function connectDB() {
  try {
    db = await mysql.createConnection(dbConfig);
    console.log('[DB] Connected to TiDB');
    console.log('[DB] Tables must be created manually in TiDB Dashboard');
  } catch (err) {
    console.error('[DB] Connection failed:', err.message);
    console.log('[DB] Retrying in 5 seconds...');
    setTimeout(connectDB, 5000);
  }
}

connectDB();

// ── Config (from TiDB) ────────────────────────
let config = {
  openThreshold:   40,
  wateringMinutes: 3,
  resetWifi:       false
};

async function loadConfig() {
  if (!db) return;
  try {
    const [rows] = await db.execute('SELECT * FROM config WHERE id = 1');
    if (rows.length) {
      config.openThreshold   = rows[0].open_threshold;
      config.wateringMinutes = rows[0].watering_minutes;
    }
  } catch (err) {
    console.error('[DB] Load config failed:', err.message);
  }
}

async function saveConfig() {
  if (!db) return;
  try {
    await db.execute(
      'UPDATE config SET open_threshold = ?, watering_minutes = ? WHERE id = 1',
      [config.openThreshold, config.wateringMinutes]
    );
  } catch (err) {
    console.error('[DB] Save config failed:', err.message);
  }
}

// ── Keep-Alive Ping ───────────────────────────
const KEEP_ALIVE_INTERVAL = 10 * 60 * 1000;
const KEEP_ALIVE_URL = process.env.RENDER_EXTERNAL_URL || `https://agriflow-mvt7.onrender.com`;

setInterval(() => {
  fetch(KEEP_ALIVE_URL)
    .then(() => console.log(`[KEEP-ALIVE] Pinged at ${new Date().toISOString()}`))
    .catch(() => {});
}, KEEP_ALIVE_INTERVAL);

// Also ping on startup
fetch(KEEP_ALIVE_URL).catch(() => {});

// ── SSE Clients ───────────────────────────────
let clients = [];
function broadcast(data) {
  const msg = `data: ${JSON.stringify(data)}\n\n`;
  clients = clients.filter(c => { try { c.write(msg); return true; } catch { return false; } });
}

// ── Soil Moisture Level ───────────────────────
function soilLevel(moisture) {
  if (moisture < 20)                         return { label: 'Very Dry',  color: '#ff4757' };
  if (moisture >= 20 && moisture < 40)       return { label: 'Dry',       color: '#ff6b35' };
  if (moisture >= 40 && moisture < 60)       return { label: 'Good',      color: '#2ed573' };
  if (moisture >= 60 && moisture < 80)       return { label: 'Moist',     color: '#00d2ff' };
  return                                            { label: 'Saturated', color: '#7b2ff7' };
}

// ── GET /api/config ───────────────────────────
app.get('/api/config', (req, res) => {
  res.json(config);
});

// ── POST /api/config ──────────────────────────
app.post('/api/config', async (req, res) => {
  const { openThreshold, wateringMinutes } = req.body;

  if (openThreshold !== undefined)   config.openThreshold   = Math.max(5, Math.min(95, parseInt(openThreshold)));
  if (wateringMinutes !== undefined) config.wateringMinutes = Math.max(1, Math.min(60, parseInt(wateringMinutes)));

  console.log(`[CONFIG] Open <${config.openThreshold}% | Water ${config.wateringMinutes} min`);
  await saveConfig();
  broadcast({ type: 'config', data: config });
  res.json({ ok: true, config });
});

// ── POST /api/reset-wifi ──────────────────────
app.post('/api/reset-wifi', async (req, res) => {
  console.log('[RESET] WiFi reset requested from dashboard');
  config.resetWifi = true;
  await saveConfig();
  broadcast({ type: 'config', data: config });
  res.json({ ok: true, message: 'ESP32 will reset WiFi on next send' });
});

// ── POST /api/sensor ──────────────────────────
app.post('/api/sensor', async (req, res) => {
  const {
    raw, moisture, valve, device, threshold, wateringMinutes: wm,
    humidity, temperature, heatIndex
  } = req.body;

  if (moisture === undefined && humidity === undefined) {
    return res.status(400).json({ error: 'Missing moisture or humidity' });
  }

  const id    = device || 'ESP32';
  const ts    = new Date().toISOString();
  const level = moisture !== undefined ? soilLevel(parseFloat(moisture)) : null;

  const reading = {
    device:      id,
    raw:         raw         !== undefined ? parseInt(raw)                  : null,
    moisture:    moisture    !== undefined ? parseFloat(moisture).toFixed(1) : null,
    valve:       valve       || 'CLOSE',
    level,
    humidity:    humidity    !== undefined ? parseFloat(humidity).toFixed(1)    : null,
    temperature: temperature !== undefined ? parseFloat(temperature).toFixed(1) : null,
    heatIndex:   heatIndex   !== undefined ? parseFloat(heatIndex).toFixed(1)   : null,
    config:      { ...config },
    timestamp:   ts
  };

  // Save to TiDB
  if (db) {
    try {
      await db.execute(
        `INSERT INTO readings (device, raw, moisture, valve, level_label, level_color, humidity, temperature, heat_index)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
        [id, reading.raw, reading.moisture, reading.valve,
         level?.label || null, level?.color || null,
         reading.humidity, reading.temperature, reading.heatIndex]
      );
    } catch (err) {
      console.error('[DB] Save reading failed:', err.message);
    }
  }

  const mPart = reading.moisture !== null ? `Moisture: ${reading.moisture}% | ` : '';
  const dhtPart = reading.humidity !== null ? `H: ${reading.humidity}% T: ${reading.temperature}°C | ` : '';
  console.log(`[${ts}] ${id} — ${mPart}${dhtPart}Valve: ${valve}${level ? ' | ' + level.label : ''}`);

  broadcast({ type: 'reading', data: reading });
  res.json({ ok: true, reading, config });
});

// ── GET /api/data ─────────────────────────────
app.get('/api/data', async (req, res) => {
  let history = [];
  let devices = {};

  if (db) {
    try {
      // Get last 200 readings
      const [rows] = await db.execute(
        'SELECT * FROM readings ORDER BY created_at DESC LIMIT 200'
      );

      history = rows.map(r => ({
        device:      r.device,
        raw:         r.raw,
        moisture:    r.moisture !== null ? String(r.moisture) : null,
        valve:       r.valve,
        level:       r.level_label ? { label: r.level_label, color: r.level_color } : null,
        humidity:    r.humidity !== null ? String(r.humidity) : null,
        temperature: r.temperature !== null ? String(r.temperature) : null,
        heatIndex:   r.heat_index !== null ? String(r.heat_index) : null,
        timestamp:   r.created_at
      }));

      // Build devices object
      for (const r of history) {
        if (!devices[r.device]) {
          devices[r.device] = { count: 0, lastSeen: r.timestamp, latest: r };
        }
        devices[r.device].count++;
      }

      // Get total count
      const [countResult] = await db.execute('SELECT COUNT(*) as total FROM readings');
      const totalCount = countResult[0].total;

      // Update device counts with total
      for (const d of Object.keys(devices)) {
        const [deviceCount] = await db.execute(
          'SELECT COUNT(*) as count FROM readings WHERE device = ?', [d]
        );
        devices[d].count = deviceCount[0].count;
      }

    } catch (err) {
      console.error('[DB] Load data failed:', err.message);
    }
  }

  res.json({ latest: history[0] || null, history, devices, config });
});

// ── SSE stream ────────────────────────────────
app.get('/api/events', (req, res) => {
  res.setHeader('Content-Type',                'text/event-stream');
  res.setHeader('Cache-Control',               'no-cache');
  res.setHeader('Connection',                  'keep-alive');
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.flushHeaders();

  // Send initial data
  (async () => {
    let initData = { latest: null, history: [], devices: {}, config };
    if (db) {
      try {
        const [rows] = await db.execute(
          'SELECT * FROM readings ORDER BY created_at DESC LIMIT 200'
        );
        initData.history = rows.map(r => ({
          device: r.device, raw: r.raw,
          moisture: r.moisture !== null ? String(r.moisture) : null,
          valve: r.valve,
          level: r.level_label ? { label: r.level_label, color: r.level_color } : null,
          humidity: r.humidity !== null ? String(r.humidity) : null,
          temperature: r.temperature !== null ? String(r.temperature) : null,
          heatIndex: r.heat_index !== null ? String(r.heat_index) : null,
          timestamp: r.created_at
        }));
        initData.latest = initData.history[0] || null;
      } catch {}
    }
    res.write(`data: ${JSON.stringify({ type: 'init', data: initData })}\n\n`);
  })();

  clients.push(res);
  const hb = setInterval(() => { try { res.write(': ping\n\n'); } catch {} }, 20000);
  req.on('close', () => { clearInterval(hb); clients = clients.filter(c => c !== res); });
});

// ── Local IP ──────────────────────────────────
function localIP() {
  for (const ifaces of Object.values(os.networkInterfaces()))
    for (const i of ifaces)
      if (i.family === 'IPv4' && !i.internal) return i.address;
  return 'localhost';
}

app.listen(PORT, '0.0.0.0', async () => {
  await loadConfig();
  const ip = localIP();
  console.log('');
  console.log('╔══════════════════════════════════════════════════╗');
  console.log('║   Agriflow — Moisture Server              ║');
  console.log('╠══════════════════════════════════════════════════╣');
  console.log(`║  Dashboard : http://localhost:${PORT}              ║`);
  console.log(`║  Network   : http://${ip}:${PORT}             ║`);
  console.log(`║  Database  : TiDB (MySQL-compatible)             ║`);
  console.log(`║  ESP32 URL : POST /api/sensor                   ║`);
  console.log('╚══════════════════════════════════════════════════╝');
  console.log('');
  console.log(`Config: Open <${config.openThreshold}% | Water ${config.wateringMinutes} min`);
  console.log('');
});
