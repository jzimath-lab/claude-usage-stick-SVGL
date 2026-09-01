'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const {
  collectCursor, serveUsageUrl, defaultVscdbPath,
  normalizeCookieHeader, resolveSession,
} = require('../server/cursor');

const NOW = Date.parse('2026-08-31T12:00:00.000Z');

describe('serveUsageUrl', () => {
  it('forces provider=cursor', () => {
    assert.equal(serveUsageUrl('http://127.0.0.1:8080'),
      'http://127.0.0.1:8080/usage?provider=cursor');
    assert.equal(serveUsageUrl('http://127.0.0.1:8080/usage?provider=codex'),
      'http://127.0.0.1:8080/usage?provider=cursor');
  });
});

describe('defaultVscdbPath', () => {
  it('uses the macOS Cursor path and ignores Linux auto-import', () => {
    assert.equal(
      defaultVscdbPath('darwin', {}, () => '/Users/you'),
      path.join('/Users/you', 'Library', 'Application Support', 'Cursor', 'User', 'globalStorage', 'state.vscdb'),
    );
    assert.equal(defaultVscdbPath('linux', {}, () => '/home/you'), null);
    assert.equal(
      defaultVscdbPath('linux', { CURSOR_VSCDB: '/tmp/state.vscdb' }, () => '/home/you'),
      '/tmp/state.vscdb',
    );
  });
});

describe('normalizeCookieHeader / resolveSession', () => {
  it('accepts a pasted Cookie header or a bare Workos token', () => {
    assert.equal(
      normalizeCookieHeader('Cookie: WorkosCursorSessionToken=abc'),
      'WorkosCursorSessionToken=abc',
    );
    assert.match(normalizeCookieHeader('just-the-value'), /WorkosCursorSessionToken=just-the-value/);
  });

  it('Linux uses the pasted cookie and never opens a browser DB', () => {
    const session = resolveSession({
      env: { CURSOR_COOKIE: 'WorkosCursorSessionToken=pasted' },
      platform: 'linux',
      execFileFn: () => { throw new Error('sqlite3 must not run on Linux cookie path'); },
    });
    assert.equal(session.via, 'cookie');
    assert.match(session.cookie, /WorkosCursorSessionToken=pasted/);
  });

  it('Linux without a pasted cookie is unavailable (no browser import)', () => {
    const session = resolveSession({
      env: {},
      platform: 'linux',
      homedirFn: () => '/home/you',
      execFileFn: () => { throw new Error('must not open Chrome/Firefox cookie DB'); },
    });
    assert.equal(session, null);
  });
});

describe('collectCursor', () => {
  it('CURSOR_FIXTURE maps CodexBar JSON (tests only)', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: { CURSOR_FIXTURE: path.join(__dirname, 'fixtures/cursor-usage.json') },
      whichFn: () => false,
      platform: 'linux',
    });
    assert.equal(snap.windows[0].usedPct, 41);
    assert.equal(snap.windows[1].usedPct, 21);
    assert.equal(snap.windows[2].usedPct, 12);
  });

  it('CODEXBAR_URL GET /usage when CLI is absent does not open vscdb', async () => {
    let sqlite = false;
    const snap = await collectCursor({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: () => false,
      platform: 'darwin',
      sqliteExecFn: () => { sqlite = true; throw new Error('vscdb should stay closed'); },
      fetchImpl: async (url) => {
        assert.equal(url, 'http://127.0.0.1:8080/usage?provider=cursor');
        return {
          ok: true,
          json: async () => ({
            provider: 'cursor',
            usage: {
              primary: { usedPercent: 17, resetsAt: '2026-09-15T00:00:00.000Z' },
              providerCost: { used: 1, limit: 10 },
            },
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 17);
    assert.equal(snap.windows[1].usedPct, 10);
    assert.equal(sqlite, false);
  });

  it('codexbar CLI is preferred over CODEXBAR_URL', async () => {
    let fetched = false;
    const snap = await collectCursor({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        assert.equal(bin, 'codexbar');
        assert.deepEqual(args, ['usage', '--format', 'json', '--provider', 'cursor']);
        cb(null, JSON.stringify({
          provider: 'cursor',
          usage: { primary: { usedPercent: 70 }, providerCost: { used: 0, limit: 5 } },
        }));
      },
      fetchImpl: async () => { fetched = true; throw new Error('serve must not run'); },
      platform: 'linux',
    });
    assert.equal(snap.windows[0].usedPct, 70);
    assert.equal(fetched, false);
  });

  it('CLI no_source continues to CODEXBAR_URL', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({ provider: 'cursor', error: 'no cursor session' }));
      },
      fetchImpl: async (url) => {
        assert.equal(url, 'http://127.0.0.1:8080/usage?provider=cursor');
        return {
          ok: true,
          json: async () => ({
            provider: 'cursor',
            usage: {
              primary: { usedPercent: 17, resetsAt: '2026-09-15T00:00:00.000Z' },
              providerCost: { used: 1, limit: 10 },
            },
          }),
        };
      },
      platform: 'linux',
    });
    assert.equal(snap.windows[0].usedPct, 17);
  });

  it('CLI on-demand USD without usedPct is sourced and does not fall through', async () => {
    let fetched = false;
    const snap = await collectCursor({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({
          provider: 'cursor',
          usage: { providerCost: { used: 4.2 } },
        }));
      },
      fetchImpl: async () => {
        fetched = true;
        return { ok: true, json: async () => ({ provider: 'cursor', error: 'no session' }) };
      },
      platform: 'linux',
    });
    assert.equal(snap.windows[1].usedAbsolute, 4.2);
    assert.equal('usedPct' in snap.windows[1], false);
    assert.equal(snap.windows[1].status, 'ok');
    assert.equal(fetched, false);
  });

  it('CLI measured 0% does not fall through to CODEXBAR_URL', async () => {
    let fetched = false;
    const snap = await collectCursor({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({
          provider: 'cursor',
          usage: { primary: { usedPercent: 0 }, providerCost: { used: 0, limit: 10 } },
        }));
      },
      fetchImpl: async () => { fetched = true; return { ok: true, json: async () => ({}) }; },
      platform: 'linux',
    });
    assert.equal(snap.windows[0].usedPct, 0);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(fetched, false);
  });

  it('codexbar CLI on PATH maps usage JSON', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        assert.equal(bin, 'codexbar');
        assert.deepEqual(args, ['usage', '--format', 'json', '--provider', 'cursor']);
        cb(null, JSON.stringify({
          provider: 'cursor',
          usage: { primary: { usedPercent: 70 }, providerCost: { used: 0, limit: 5 } },
        }));
      },
      platform: 'linux',
    });
    assert.equal(snap.windows[0].usedPct, 70);
    assert.equal(snap.windows[0].status, 'warning');
    assert.equal(snap.windows[1].usedPct, 0);
    assert.equal(snap.windows.length, 2);
  });

  it('pasted cookie → usage-summary + sand-usage', async () => {
    const calls = [];
    const snap = await collectCursor({
      now: NOW,
      env: {
        CURSOR_COOKIE: 'WorkosCursorSessionToken=sess',
        CURSOR_USAGE_URL: 'https://cursor.com/api/usage-summary',
        CURSOR_SAND_URL: 'https://cursor.com/api/dashboard/get-sand-usage-status',
      },
      whichFn: () => false,
      platform: 'linux',
      fetchImpl: async (url, opts) => {
        calls.push({ url, method: opts.method || 'GET', origin: opts.headers.Origin });
        assert.equal(opts.headers.Cookie, 'WorkosCursorSessionToken=sess');
        if (url.endsWith('/usage-summary')) {
          return {
            ok: true,
            json: async () => ({
              billingCycleEnd: '2026-09-15T00:00:00.000Z',
              individualUsage: {
                plan: { totalPercentUsed: 33 },
                onDemand: { used: 100, limit: 400 },
              },
            }),
          };
        }
        assert.equal(opts.method, 'POST');
        assert.equal(opts.headers.Origin, 'https://cursor.com');
        return {
          ok: true,
          json: async () => ({ usagePercent: 8, nextResetTimestampUtc: '2026-09-07T00:00:00.000Z' }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 33);
    assert.equal(snap.windows[1].usedPct, 25);
    assert.equal(snap.windows[2].usedPct, 8);
    assert.equal(calls.length, 2);
  });

  it('sand POST failure keeps monthly bars and omits Grok', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: { CURSOR_COOKIE: 'WorkosCursorSessionToken=sess' },
      whichFn: () => false,
      platform: 'linux',
      fetchImpl: async (url) => {
        if (String(url).includes('usage-summary')) {
          return {
            ok: true,
            json: async () => ({
              individualUsage: { plan: { totalPercentUsed: 22 }, onDemand: { used: 0, limit: 1000 } },
            }),
          };
        }
        return { ok: false, status: 502 };
      },
    });
    assert.equal(snap.windows[0].usedPct, 22);
    assert.equal(snap.windows[1].usedPct, 0);
    assert.equal(snap.windows.length, 2);
    assert.ok(!snap.windows.some((w) => w.name === 'grok_bot'));
  });

  it('usage-summary 401 → no_source, cookie is not in the error', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: { CURSOR_COOKIE: 'WorkosCursorSessionToken=super-secret-cookie' },
      whichFn: () => false,
      platform: 'linux',
      fetchImpl: async () => ({ ok: false, status: 401 }),
    });
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.match(snap.error, /usage_summary_http_401/);
    assert.equal(String(snap.error).includes('super-secret-cookie'), false);
  });

  it('Linux without cookie / CodexBar is honest no_source (no browser import)', async () => {
    const snap = await collectCursor({
      now: NOW,
      env: {},
      whichFn: () => false,
      platform: 'linux',
      sqliteExecFn: () => { throw new Error('must not open vscdb or cookie DB'); },
      homedirFn: () => '/home/you',
    });
    assert.equal(snap.error, 'cursor_cookie_required');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
  });

  it('macOS vscdb token → usage-summary when CodexBar is absent', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cursor-vscdb-'));
    const db = path.join(dir, 'state.vscdb');
    fs.writeFileSync(db, 'placeholder');
    const snap = await collectCursor({
      now: NOW,
      env: { CURSOR_VSCDB: db, CURSOR_USAGE_URL: 'https://cursor.com/api/usage-summary' },
      whichFn: () => false,
      platform: 'darwin',
      sqliteExecFn: (bin, args) => {
        assert.equal(bin, 'sqlite3');
        assert.ok(args.includes(db));
        assert.ok(args.some((a) => String(a).includes('cursorAuth/accessToken')));
        return 'eyJhbGciOiJub25lIn0.eyJzdWIiOiJhdXRofHVzZXItMSIsImV4cCI6NDgwMDAwMDAwMH0.';
      },
      fetchImpl: async (url, opts) => {
        assert.match(opts.headers.Cookie, /WorkosCursorSessionToken=/);
        assert.equal(opts.headers.Cookie.includes('eyJ'), true);
        if (String(url).includes('usage-summary')) {
          return {
            ok: true,
            json: async () => ({ individualUsage: { plan: { totalPercentUsed: 9 } } }),
          };
        }
        return { ok: true, json: async () => ({}) };
      },
    });
    assert.equal(snap.windows[0].usedPct, 9);
    assert.equal(snap.via, 'vscdb');
    fs.rmSync(dir, { recursive: true });
  });
});
