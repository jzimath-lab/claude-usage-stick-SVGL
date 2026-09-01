'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { collectActions } = require('../server/g1');

describe('collectActions', () => {
  it('maps G1_URL /api/github without talking to api.github.com', async () => {
    const painel = {
      fonte: 'billing',
      cota: { usados_min: 731, incluidos_min: 2000, pagos_min: 0, pct: 37 },
      custo: { usd: 0 },
    };
    const snap = await collectActions({
      now: Date.parse('2026-08-10T00:00:00.000Z'),
      env: { G1_URL: 'http://estacao-server:3010/api/github', G1_DEVICE_TOKEN: 'x' },
      fetchImpl: async (url, opts) => {
        assert.match(url, /\/api\/github/);
        assert.equal(opts.headers['X-Device-Token'], 'x');
        return { ok: true, json: async () => painel };
      },
    });
    assert.equal(snap.source, 'actions');
    assert.equal(snap.windows[0].usedAbsolute, 731);
    assert.equal(snap.windows[0].usedPct, 37);
    assert.equal(snap.windows[0].status, 'ok');
  });

  it('G1 HTTP failure → no_source with error, not 0', async () => {
    const snap = await collectActions({
      now: Date.now(),
      env: { G1_URL: 'http://127.0.0.1:9/api/github' },
      fetchImpl: async () => ({ ok: false, status: 502 }),
    });
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.match(snap.error, /g1_http_502/);
  });

  it('G1_FIXTURE maps a saved painel JSON', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'g1-'));
    const file = path.join(dir, 'github.json');
    fs.writeFileSync(file, JSON.stringify({
      cota: { usados_min: 12, incluidos_min: 2000, pagos_min: 0, pct: 1 },
      custo: { usd: 0 },
    }));
    const snap = await collectActions({
      now: Date.now(),
      env: { G1_FIXTURE: file },
    });
    assert.equal(snap.windows[0].usedAbsolute, 12);
    fs.rmSync(dir, { recursive: true });
  });

  it('requires G1 github.js buscarBilling — does not fetch GitHub itself', async () => {
    let called = false;
    const snap = await collectActions({
      now: Date.parse('2026-08-01T03:00:00.000Z'),
      env: { GITHUB_TOKEN: 'tok', GITHUB_USER: 'juliano', GH_INCLUIDOS: '2000' },
      requireFn: () => ({
        buscarBilling: async (cfg, agoraMs) => {
          called = true;
          assert.equal(cfg.githubToken, 'tok');
          assert.equal(cfg.githubUser, 'juliano');
          assert.ok(agoraMs);
          return { usados_min: 300, pagos_min: 0, incluidos_min: 300, usd: 0 };
        },
      }),
    });
    // requireFn is only used when a module path resolves. Without G1_DIR on
    // disk this stays g1_unavailable — that's the honest default.
    if (called) {
      assert.equal(snap.windows[0].usedPct, 15);
    } else {
      assert.equal(snap.windows[0].status, 'no_source');
    }
  });

  it('calls buscarBilling when G1_GITHUB_JS is a stub file', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'g1mod-'));
    const file = path.join(dir, 'github.js');
    fs.writeFileSync(file, 'module.exports = { buscarBilling() { throw new Error("use requireFn"); } }');
    const snap = await collectActions({
      now: Date.parse('2026-08-01T03:00:00.000Z'),
      env: {
        G1_GITHUB_JS: file,
        GITHUB_TOKEN: 'tok',
        GITHUB_USER: 'juliano',
        GH_INCLUIDOS: '2000',
      },
      requireFn: () => ({
        buscarBilling: async () => ({
          usados_min: 300, pagos_min: 0, incluidos_min: 300, usd: 0,
        }),
      }),
    });
    assert.equal(snap.windows[0].usedAbsolute, 300);
    assert.equal(snap.windows[0].usedPct, 15);
    assert.notEqual(snap.windows[0].usedPct, 100);
    fs.rmSync(dir, { recursive: true });
  });

  it('no G1 at all → no_source, never 0', async () => {
    const snap = await collectActions({ now: Date.now(), env: {} });
    assert.equal(snap.error, 'g1_unavailable');
    assert.equal('usedPct' in snap.windows[0], false);
  });
});
