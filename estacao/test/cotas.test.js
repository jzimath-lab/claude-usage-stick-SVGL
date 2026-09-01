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
      collectCodexFn: async () => { throw new Error('codex down'); },
      collectCursorFn: async () => { throw new Error('cursor down'); },
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
      collectCodexFn: async ({ now }) => noSource('codex', new Date(now).toISOString()),
      collectCursorFn: async ({ now }) => noSource('cursor', new Date(now).toISOString()),
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

  it('live Codex does not invent numbers for Cursor/Gemini and keeps Actions', async () => {
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
      collectCodexFn: async ({ now }) => ({
        source: 'codex',
        label: 'Codex',
        windows: [
          { name: '5h', usedPct: 28, resetAt: '2026-08-31T19:15:00.000Z', status: 'ok' },
          { name: '7d', usedPct: 59, resetAt: '2026-09-05T17:00:00.000Z', status: 'ok' },
        ],
        asOf: new Date(now).toISOString(),
      }),
      collectCursorFn: async ({ now }) => noSource('cursor', new Date(now).toISOString()),
    });
    await col.refreshAll();
    const p = col.payload();
    const codex = p.sources.find((s) => s.source === 'codex');
    assert.equal(codex.windows[0].usedPct, 28);
    assert.equal(codex.windows[0].resetAt, '2026-08-31T19:15:00.000Z');
    assert.equal(p.sources.find((s) => s.source === 'actions').windows[0].usedPct, 37);
    for (const id of ['claude', 'cursor', 'gemini']) {
      const s = p.sources.find((x) => x.source === id);
      assert.equal(s.windows[0].status, 'no_source');
      assert.equal('usedPct' in s.windows[0], false);
    }
    col.stop();
  });

  it('Codex failure leaves Actions live and Cursor/Gemini no_source', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async ({ now }) => ({
        source: 'actions',
        label: 'GitHub Actions',
        windows: [{ name: 'minutos', usedPct: 10, usedAbsolute: 200, unit: 'min', status: 'ok' }],
        asOf: new Date(now).toISOString(),
      }),
      collectCodexFn: async () => { throw new Error('wham down'); },
      collectCursorFn: async ({ now }) => noSource('cursor', new Date(now).toISOString()),
    });
    await col.refreshAll();
    const p = col.payload();
    assert.equal(p.sources.find((s) => s.source === 'actions').windows[0].usedPct, 10);
    const codex = p.sources.find((s) => s.source === 'codex');
    assert.equal(codex.windows[0].status, 'no_source');
    assert.equal('usedPct' in codex.windows[0], false);
    col.stop();
  });

  it('live Cursor does not invent numbers for Gemini and keeps Actions/Codex', async () => {
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
      collectCodexFn: async ({ now }) => ({
        source: 'codex',
        label: 'Codex',
        windows: [
          { name: '5h', usedPct: 28, resetAt: '2026-08-31T19:15:00.000Z', status: 'ok' },
          { name: '7d', usedPct: 59, status: 'ok' },
        ],
        asOf: new Date(now).toISOString(),
      }),
      collectCursorFn: async ({ now }) => ({
        source: 'cursor',
        label: 'Cursor',
        windows: [
          { name: 'incluido', usedPct: 41, resetAt: '2026-09-15T00:00:00.000Z', status: 'ok' },
          { name: 'on_demand', usedPct: 21, usedAbsolute: 4.2, unit: 'usd', status: 'ok' },
          { name: 'grok_bot', usedPct: 12, resetAt: '2026-09-07T00:00:00.000Z', status: 'ok' },
        ],
        asOf: new Date(now).toISOString(),
      }),
    });
    await col.refreshAll();
    const p = col.payload();
    const cursor = p.sources.find((s) => s.source === 'cursor');
    assert.equal(cursor.windows[0].usedPct, 41);
    assert.equal(cursor.windows[1].usedPct, 21);
    assert.equal(cursor.windows[2].usedPct, 12);
    assert.equal(p.sources.find((s) => s.source === 'actions').windows[0].usedPct, 37);
    assert.equal(p.sources.find((s) => s.source === 'codex').windows[0].usedPct, 28);
    const gemini = p.sources.find((s) => s.source === 'gemini');
    assert.equal(gemini.windows[0].status, 'no_source');
    assert.equal('usedPct' in gemini.windows[0], false);
    col.stop();
  });

  it('Cursor failure leaves Actions/Codex live and Gemini no_source', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async ({ now }) => ({
        source: 'actions',
        label: 'GitHub Actions',
        windows: [{ name: 'minutos', usedPct: 10, usedAbsolute: 200, unit: 'min', status: 'ok' }],
        asOf: new Date(now).toISOString(),
      }),
      collectCodexFn: async ({ now }) => ({
        source: 'codex',
        label: 'Codex',
        windows: [{ name: '5h', usedPct: 28, status: 'ok' }],
        asOf: new Date(now).toISOString(),
      }),
      collectCursorFn: async () => { throw new Error('sand down'); },
    });
    await col.refreshAll();
    const p = col.payload();
    assert.equal(p.sources.find((s) => s.source === 'actions').windows[0].usedPct, 10);
    assert.equal(p.sources.find((s) => s.source === 'codex').windows[0].usedPct, 28);
    const cursor = p.sources.find((s) => s.source === 'cursor');
    assert.equal(cursor.windows[0].status, 'no_source');
    assert.equal('usedPct' in cursor.windows[0], false);
    col.stop();
  });
});

describe('GET /cotas', () => {
  it('serves the cached payload without blocking on G1', async () => {
    const col = createCollector({
      pollMs: 60_000,
      collectActionsFn: async () => noSource('actions', new Date().toISOString(), 'slow'),
      collectCodexFn: async () => noSource('codex', new Date().toISOString(), 'slow'),
      collectCursorFn: async () => noSource('cursor', new Date().toISOString(), 'slow'),
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
