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

describe('estacao server (G1_FIXTURE)', () => {
  const port = 18787;
  const child = spawn(process.execPath, [path.join(__dirname, '../server/index.js')], {
    env: {
      ...process.env,
      PORT: String(port),
      HOST: '127.0.0.1',
      G1_FIXTURE: path.join(__dirname, 'fixtures/g1-github.json'),
      POLL_MS: '60000',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  after(() => { child.kill('SIGTERM'); });

  it('GET /cotas paints Actions from G1 and leaves the rest no_source', async () => {
    await waitFor(`http://127.0.0.1:${port}/health`);
    // first Actions poll is async — wait until usedAbsolute lands
    let body;
    for (let i = 0; i < 40; i++) {
      const r = await getJson(`http://127.0.0.1:${port}/cotas`);
      assert.equal(r.status, 200);
      body = r.body;
      const act = body.sources.find((s) => s.source === 'actions');
      if (act && act.windows[0].usedAbsolute === 731) break;
      await new Promise((x) => setTimeout(x, 50));
    }
    const actions = body.sources.find((s) => s.source === 'actions');
    assert.equal(actions.windows[0].usedAbsolute, 731);
    assert.equal(actions.windows[0].usedPct, 37);
    assert.equal(actions.windows[1].usedAbsolute, 0);
    assert.equal(actions.windows[1].unit, 'usd');
    for (const id of ['claude', 'codex', 'cursor', 'gemini']) {
      const s = body.sources.find((x) => x.source === id);
      assert.equal(s.windows[0].status, 'no_source');
      assert.equal('usedPct' in s.windows[0], false);
    }
  });
});
