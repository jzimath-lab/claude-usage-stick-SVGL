'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const http = require('http');
const { createCollector } = require('../server/cotas');
const { noSource } = require('../server/snapshot');

function listen(handler) {
  return new Promise((resolve) => {
    const s = http.createServer(handler);
    s.listen(0, '127.0.0.1', () => {
      const { port } = s.address();
      resolve({
        url: `http://127.0.0.1:${port}`,
        close: () => new Promise((r) => s.close(r)),
      });
    });
  });
}

function getJson(url) {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let b = '';
      res.on('data', (c) => { b += c; });
      res.on('end', () => {
        try { resolve({ status: res.statusCode, body: JSON.parse(b) }); }
        catch (e) { reject(e); }
      });
    }).on('error', reject);
  });
}

describe('collector independence', () => {
  it('Actions failure leaves Codex/Cursor/Gemini as no_source, not 0', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async () => { throw new Error('g1 down'); },
    });
    await col.refreshAll();
    const p = col.payload();
    const actions = p.sources.find((s) => s.source === 'actions');
    const codex = p.sources.find((s) => s.source === 'codex');
    assert.equal(actions.windows[0].status, 'no_source');
    assert.equal(codex.windows[0].status, 'no_source');
    assert.equal('usedPct' in actions.windows[0], false);
    assert.equal('usedPct' in codex.windows[0], false);
    col.stop();
  });

  it('a live Actions snapshot does not invent numbers for other sources', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async ({ now }) => ({
        source: 'actions',
        label: 'GitHub Actions',
        windows: [
          { name: 'minutos', usedPct: 37, usedAbsolute: 731, unit: 'min', status: 'ok' },
          { name: 'a_pagar', usedAbsolute: 0, unit: 'usd', status: 'ok' },
        ],
        asOf: new Date(now).toISOString(),
      }),
    });
    await col.refreshAll();
    const p = col.payload();
    assert.equal(p.sources.find((s) => s.source === 'actions').windows[0].usedPct, 37);
    for (const id of ['claude', 'codex', 'cursor', 'gemini']) {
      const s = p.sources.find((x) => x.source === id);
      assert.equal(s.windows[0].status, 'no_source');
      assert.equal('usedPct' in s.windows[0], false);
    }
    col.stop();
  });
});

describe('GET /cotas', () => {
  it('serves the cached payload without blocking on G1', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async () => noSource('actions', new Date().toISOString(), 'slow'),
    });
    await col.refreshAll();

    const srv = await listen((req, res) => {
      if (req.url === '/cotas') {
        const body = JSON.stringify(col.payload());
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(body);
      } else {
        res.writeHead(404); res.end();
      }
    });
    const { status, body } = await getJson(`${srv.url}/cotas`);
    assert.equal(status, 200);
    assert.ok(Array.isArray(body.sources));
    assert.equal(body.sources.length, 5);
    await srv.close();
    col.stop();
  });
});
