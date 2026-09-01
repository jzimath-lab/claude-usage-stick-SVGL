'use strict';

/**
 * Estação LAN: GET /cotas + mDNS instance `estacao` (inverted claude-stick.local).
 * Stick paints. Secrets stay in env, never git. The ESP32 never opens
 * state.vscdb, cookie DBs, auth.json, or JSONL.
 */

const http = require('http');
const { createCollector } = require('./cotas');

function loadDotEnv({ filePath, env = process.env } = {}) {
  const fs = require('fs');
  const path = require('path');
  const p = filePath || path.join(__dirname, '..', '.env');
  if (!fs.existsSync(p)) return env;
  for (const line of fs.readFileSync(p, 'utf8').split('\n')) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    const i = t.indexOf('=');
    if (i < 1) continue;
    const k = t.slice(0, i).trim();
    let v = t.slice(i + 1).trim();
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'")))
      v = v.slice(1, -1);
    if (env[k] == null) env[k] = v;
  }
  return env;
}

/** PORT / HOST / MDNS_NAME / POLL_MS — read AFTER loadDotEnv so estacao/.env applies. */
function stationConfig(env = process.env) {
  return {
    port: Number(env.PORT) || 8787,
    host: env.HOST || '0.0.0.0',
    mdnsName: env.MDNS_NAME || 'estacao',
    pollMs: Number(env.POLL_MS) || 90_000,
  };
}

function advertise(port, mdnsName) {
  try {
    const { Bonjour } = require('bonjour-service');
    const b = new Bonjour();
    b.publish({
      name: mdnsName,
      type: 'http',
      protocol: 'tcp',
      port,
      txt: { path: '/cotas' },
    });
    console.log(`[mdns] instance=${mdnsName}  _http._tcp  :${port}  path=/cotas`);
    return b;
  } catch (e) {
    console.warn('[mdns] not advertising:', e.message);
    return null;
  }
}

function main() {
  loadDotEnv();
  const { port, host, mdnsName, pollMs } = stationConfig();
  const collector = createCollector({ pollMs });
  collector.start();

  const server = http.createServer((req, res) => {
    const url = (req.url || '/').split('?')[0];
    const json = (code, obj) => {
      const body = JSON.stringify(obj);
      res.writeHead(code, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store',
      });
      res.end(body);
    };

    if (req.method === 'GET' && (url === '/cotas' || url === '/api/cotas')) {
      return json(200, collector.payload());
    }
    if (req.method === 'GET' && (url === '/health' || url === '/api/health')) {
      return json(200, { ok: true, mdns: `${mdnsName}.local`, pollMs });
    }
    return json(404, { error: 'not_found' });
  });

  server.listen(port, host, () => {
    console.log(`[cotas] GET http://${host}:${port}/cotas`);
    advertise(port, mdnsName);
  });

  const shutdown = () => {
    collector.stop();
    server.close(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

if (require.main === module) main();

module.exports = { main, loadDotEnv, stationConfig };
