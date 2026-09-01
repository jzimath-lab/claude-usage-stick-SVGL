'use strict';

const { describe, it, after } = require('node:test');
const assert = require('node:assert/strict');
const http = require('http');
const { spawn } = require('child_process');
const path = require('path');

function getJson(url) {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let b = '';
      res.on('data', (c) => { b += c; });
      res.on('end', () => resolve({ status: res.statusCode, body: JSON.parse(b) }));
    }).on('error', reject);
  });
}

function waitFor(url, ms = 4000) {
  const start = Date.now();
  return new Promise((resolve, reject) => {
    const tick = () => {
      http.get(url, (res) => {
        res.resume();
        if (res.statusCode === 200) return resolve();
        if (Date.now() - start > ms) return reject(new Error('timeout'));
        setTimeout(tick, 50);
      }).on('error', () => {
        if (Date.now() - start > ms) return reject(new Error('timeout'));
        setTimeout(tick, 50);
      });
    };
    tick();
  });
}

describe('estacao server (G1 + Codex + Cursor + Gemini fixtures)', () => {
  const port = 18787;
  const child = spawn(process.execPath, [path.join(__dirname, '../server/index.js')], {
    env: {
      ...process.env,
      PORT: String(port),
      HOST: '127.0.0.1',
      G1_FIXTURE: path.join(__dirname, 'fixtures/g1-github.json'),
      CODEX_FIXTURE: path.join(__dirname, 'fixtures/codexbar-usage.json'),
      CURSOR_FIXTURE: path.join(__dirname, 'fixtures/cursor-usage.json'),
      GEMINI_FIXTURE: path.join(__dirname, 'fixtures/gemini-usage.json'),
      POLL_MS: '60000',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  after(() => { child.kill('SIGTERM'); });

  it('GET /cotas paints Actions + Codex + Cursor + Gemini', async () => {
    await waitFor(`http://127.0.0.1:${port}/health`);
    let body;
    for (let i = 0; i < 40; i++) {
      const r = await getJson(`http://127.0.0.1:${port}/cotas`);
      assert.equal(r.status, 200);
      body = r.body;
      const act = body.sources.find((s) => s.source === 'actions');
      const cd = body.sources.find((s) => s.source === 'codex');
      const cur = body.sources.find((s) => s.source === 'cursor');
      const gm = body.sources.find((s) => s.source === 'gemini');
      if (act && act.windows[0].usedAbsolute === 731
          && cd && cd.windows[0].usedPct === 28
          && cur && cur.windows[0].usedPct === 41
          && gm && gm.windows[0].usedPct === 42) break;
      await new Promise((x) => setTimeout(x, 50));
    }
    const actions = body.sources.find((s) => s.source === 'actions');
    assert.equal(actions.windows[0].usedAbsolute, 731);
    assert.equal(actions.windows[0].usedPct, 37);
    assert.equal(actions.windows[1].usedAbsolute, 0);
    assert.equal(actions.windows[1].unit, 'usd');
    const codex = body.sources.find((s) => s.source === 'codex');
    assert.equal(codex.windows[0].usedPct, 28);
    assert.equal(codex.windows[0].resetAt, '2026-08-31T19:15:00.000Z');
    assert.equal(codex.windows[1].usedPct, 59);
    const cursor = body.sources.find((s) => s.source === 'cursor');
    assert.equal(cursor.windows[0].usedPct, 41);
    assert.equal(cursor.windows[1].usedPct, 21);
    assert.equal(cursor.windows[2].usedPct, 12);
    const gemini = body.sources.find((s) => s.source === 'gemini');
    assert.equal(gemini.windows[0].name, 'hoje');
    assert.equal(gemini.windows[0].usedPct, 42);
    assert.equal(gemini.windows[1].usedPct, 15);
    const claude = body.sources.find((s) => s.source === 'claude');
    assert.equal(claude.windows[0].status, 'no_source');
    assert.equal('usedPct' in claude.windows[0], false);
  });
});
