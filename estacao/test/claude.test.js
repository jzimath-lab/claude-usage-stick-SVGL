'use strict';

/**
 * Claude collector — the source the ZYN-568 migration left behind.
 *
 * Measured on 2026-09-17: `claude` sat in SOURCES with a permanent no_source
 * stub while refreshOne() only knew actions/codex/cursor/gemini. The metric
 * the device is NAMED AFTER never reached the screen.
 *
 * Why `codexbar dashboard` and not `codexbar usage --provider claude`: the
 * `usage` command looks for the sessionKey in BROWSER cookies and fails with
 * "No Claude session key found"; `dashboard` reads the CodexBar app config,
 * where tools/atualiza-cookie.js writes the pasted key. Measured both.
 *
 * Why the collector THROWS on failure instead of returning noSource: the web
 * source FLAPS (same afternoon: 3 windows at 18:18, cli error at 18:44).
 * refreshOne()'s catch path keeps the last good snapshot; a returned noSource
 * would REPLACE it. Flap must degrade to stale, never to blank.
 */

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { mapClaudeFromDashboard } = require('../server/snapshot');
const { collectClaude } = require('../server/claude');

const NOW = Date.parse('2026-09-17T21:05:00Z');
const DASH = fs.readFileSync(path.join(__dirname, 'fixtures/dashboard-claude.json'), 'utf8');

function execOk(bin, args, opts, cb) { cb(null, DASH, ''); }

describe('mapClaudeFromDashboard', () => {
  it('maps session->5h and weekly->7d BY KIND, never by position', () => {
    const s = mapClaudeFromDashboard(JSON.parse(DASH), NOW);
    assert.equal(s.source, 'claude');
    assert.equal(s.windows[0].name, '5h');
    assert.equal(s.windows[0].usedPct, 24);
    assert.equal(s.windows[0].status, 'ok');
    assert.equal(s.windows[0].resetAt, '2026-09-17T23:00:00.000Z');
    assert.equal(s.windows[1].name, '7d');
    assert.equal(s.windows[1].usedPct, 13);
    assert.equal(s.via, 'web');
  });

  it('carries the third window (weekly_opus) instead of dropping it', () => {
    const s = mapClaudeFromDashboard(JSON.parse(DASH), NOW);
    const extra = s.windows.find((w) => w.name === 'weekly_opus');
    assert.ok(extra, 'extra window must survive the mapping');
    assert.equal(extra.usedPct, 5);
  });

  it('claude entry with error and no windows -> noSource with the message', () => {
    const doc = { providers: [{ id: 'claude', windows: [],
      error: { message: 'Could not parse Claude usage: Missing Current session.' } }] };
    const s = mapClaudeFromDashboard(doc, NOW);
    assert.equal(s.windows[0].status, 'no_source');
    assert.match(s.error, /Missing Current session/);
  });

  it('never invents 0: window without usedPercent stays no_source', () => {
    const doc = { providers: [{ id: 'claude', windows: [{ kind: 'session' }] }] };
    const s = mapClaudeFromDashboard(doc, NOW);
    assert.equal(s.windows[0].status, 'no_source');
    assert.equal('usedPct' in s.windows[0], false);
  });
});

describe('collectClaude', () => {
  it('runs `codexbar dashboard` and maps the claude block', async () => {
    let seen;
    const snap = await collectClaude({
      now: NOW, state: { at: 0 }, whichFn: () => true,
      execFileFn: (bin, args, opts, cb) => { seen = [bin, ...args]; execOk(bin, args, opts, cb); },
    });
    assert.equal(seen[0], 'codexbar');
    assert.equal(seen[1], 'dashboard');
    assert.ok(seen.includes('redacted'), 'identity stays redacted');
    assert.equal(snap.windows[0].usedPct, 24);
  });

  it('THROWS on flap so refreshOne keeps the last good snapshot', async () => {
    const doc = JSON.stringify({ providers: [{ id: 'claude', windows: [],
      error: { message: 'Missing Current session.' } }] });
    await assert.rejects(
      collectClaude({ now: NOW, state: { at: 0 }, whichFn: () => true,
        execFileFn: (b, a, o, cb) => cb(null, doc, '') }),
      /Missing Current session/,
    );
  });

  it('throttles: a fresh success suppresses the next dashboard run', async () => {
    let runs = 0;
    const state = { at: 0 };
    const opts = { whichFn: () => true, state,
      execFileFn: (b, a, o, cb) => { runs++; execOk(b, a, o, cb); } };
    const first = await collectClaude({ ...opts, now: NOW });
    const second = await collectClaude({ ...opts, now: NOW + 90_000 });
    assert.ok(first, 'first run collects');
    assert.equal(second, null, 'null = keep cache, no update');
    assert.equal(runs, 1, 'dashboard must not run twice inside the window');
  });

  it('failure does NOT arm the throttle: next poll retries', async () => {
    const bad = JSON.stringify({ providers: [{ id: 'claude', windows: [] }] });
    const state = { at: 0 };
    let runs = 0;
    const opts = { whichFn: () => true, state,
      execFileFn: (b, a, o, cb) => { runs++; cb(null, runs === 1 ? bad : DASH, ''); } };
    await assert.rejects(collectClaude({ ...opts, now: NOW }));
    const snap = await collectClaude({ ...opts, now: NOW + 90_000 });
    assert.ok(snap, 'retry after failure must collect');
    assert.equal(runs, 2);
  });

  it('codexbar off PATH throws codexbar_missing', async () => {
    await assert.rejects(
      collectClaude({ now: NOW, state: { at: 0 }, whichFn: () => false }),
      /codexbar_missing/,
    );
  });
});
