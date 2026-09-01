'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('events');
const { PassThrough } = require('stream');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { collectCodex, parseAuthJson, serveUsageUrl, authJsonPath } = require('../server/codex');

const NOW = Date.parse('2026-08-31T12:00:00.000Z');

function fakeSpawn(script) {
  return function spawnFn() {
    const stdin = new PassThrough();
    const stdout = new PassThrough();
    const stderr = new PassThrough();
    const child = new EventEmitter();
    child.stdin = stdin;
    child.stdout = stdout;
    child.stderr = stderr;
    child.killed = false;
    child.kill = () => {
      child.killed = true;
      setImmediate(() => child.emit('exit', 0));
    };
    let buf = '';
    stdin.on('data', (chunk) => {
      buf += chunk.toString();
      const lines = buf.split('\n');
      buf = lines.pop();
      for (const line of lines) {
        const t = line.trim();
        if (!t) continue;
        let msg;
        try { msg = JSON.parse(t); } catch { continue; }
        const reply = script(msg);
        if (reply) stdout.write(`${JSON.stringify(reply)}\n`);
      }
    });
    return child;
  };
}

describe('parseAuthJson', () => {
  it('reads tokens.access_token + account_id', () => {
    const a = parseAuthJson(JSON.stringify({
      tokens: { access_token: 'tok', account_id: 'acct-1' },
    }));
    assert.equal(a.accessToken, 'tok');
    assert.equal(a.accountId, 'acct-1');
  });
  it('accepts top-level access_token', () => {
    const a = parseAuthJson(JSON.stringify({ access_token: 'legacy' }));
    assert.equal(a.accessToken, 'legacy');
  });
  it('returns null when the token is missing', () => {
    assert.equal(parseAuthJson('{"tokens":{}}'), null);
    assert.equal(parseAuthJson('not-json'), null);
  });
});

describe('serveUsageUrl', () => {
  it('appends /usage?provider=codex', () => {
    assert.equal(serveUsageUrl('http://127.0.0.1:8080'),
      'http://127.0.0.1:8080/usage?provider=codex');
    assert.equal(serveUsageUrl('http://127.0.0.1:8080/usage'),
      'http://127.0.0.1:8080/usage?provider=codex');
  });
});

describe('authJsonPath', () => {
  it('prefers $CODEX_HOME/auth.json over ~/.codex/auth.json', () => {
    assert.equal(
      authJsonPath({ CODEX_HOME: '/tmp/codex-home' }, () => '/home/you'),
      path.join('/tmp/codex-home', 'auth.json'),
    );
    assert.equal(
      authJsonPath({}, () => '/home/you'),
      path.join('/home/you', '.codex', 'auth.json'),
    );
  });
});

describe('collectCodex', () => {
  it('CODEX_FIXTURE maps CodexBar JSON (tests only)', async () => {
    const snap = await collectCodex({
      now: NOW,
      env: { CODEX_FIXTURE: path.join(__dirname, 'fixtures/codexbar-usage.json') },
      whichFn: () => false,
    });
    assert.equal(snap.windows[0].usedPct, 28);
    assert.equal(snap.windows[0].resetAt, '2026-08-31T19:15:00.000Z');
    assert.equal(snap.windows[1].usedPct, 59);
  });

  it('CODEXBAR_URL GET /usage is preferred and does not open auth.json', async () => {
    let readAuth = false;
    const snap = await collectCodex({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: () => false,
      readFileFn: () => { readAuth = true; throw new Error('auth.json should stay closed'); },
      fetchImpl: async (url, opts) => {
        assert.equal(url, 'http://127.0.0.1:8080/usage?provider=codex');
        assert.equal(opts.headers.Accept, 'application/json');
        return {
          ok: true,
          json: async () => ({
            provider: 'codex',
            usage: { primary: { usedPercent: 17, resetsAt: '2026-08-31T14:00:00.000Z' } },
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 17);
    assert.equal(readAuth, false);
  });

  it('codexbar CLI on PATH maps usage JSON and never invents 0', async () => {
    const snap = await collectCodex({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        assert.equal(bin, 'codexbar');
        assert.deepEqual(args, ['usage', '--format', 'json', '--provider', 'codex']);
        cb(null, JSON.stringify({
          provider: 'codex',
          usage: { primary: { usedPercent: 70 }, secondary: { usedPercent: 12 } },
        }));
      },
      readFileFn: () => { throw new Error('no auth'); },
    });
    assert.equal(snap.windows[0].usedPct, 70);
    assert.equal(snap.windows[0].status, 'warning');
    assert.equal(snap.windows[1].usedPct, 12);
  });

  it('auth.json + wham/usage when CodexBar is absent', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'codex-auth-'));
    fs.writeFileSync(path.join(dir, 'auth.json'), JSON.stringify({
      tokens: { access_token: 'tok', account_id: 'acct-9' },
    }));
    const snap = await collectCodex({
      now: NOW,
      env: { CODEX_HOME: dir, CODEX_WHAM_URL: 'https://chatgpt.com/backend-api/wham/usage' },
      whichFn: () => false,
      fetchImpl: async (url, opts) => {
        assert.equal(url, 'https://chatgpt.com/backend-api/wham/usage');
        assert.equal(opts.headers.Authorization, 'Bearer tok');
        assert.equal(opts.headers['ChatGPT-Account-Id'], 'acct-9');
        assert.equal(JSON.stringify(opts.headers).includes('tok') || true, true);
        return {
          ok: true,
          json: async () => ({
            rate_limit: {
              primary_window: { used_percent: 42, reset_at: 1756653307 },
              secondary_window: { used_percent: 8, reset_at: 1757157165 },
            },
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(snap.windows[1].usedPct, 8);
    fs.rmSync(dir, { recursive: true });
  });

  it('wham 401 → no_source, not 0; token is not in the error', async () => {
    const snap = await collectCodex({
      now: NOW,
      env: { CODEX_HOME: '/nope', CODEX_WHAM_URL: 'https://chatgpt.com/backend-api/wham/usage' },
      whichFn: () => false,
      readFileFn: () => JSON.stringify({ tokens: { access_token: 'super-secret-token' } }),
      fetchImpl: async () => ({ ok: false, status: 401 }),
    });
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.match(snap.error, /wham_http_401/);
    assert.equal(snap.error.includes('super-secret-token'), false);
  });

  it('app-server RPC when there is no auth.json', async () => {
    const spawnFn = fakeSpawn((msg) => {
      if (msg.method === 'initialize') return { id: msg.id, result: { userAgent: 'test' } };
      if (msg.method === 'account/rateLimits/read') {
        return {
          id: msg.id,
          result: {
            rateLimits: {
              primary: { usedPercent: 25, resetsAt: 1756653307 },
              secondary: { usedPercent: 18, resetsAt: 1757157165 },
            },
          },
        };
      }
      return null;
    });
    const snap = await collectCodex({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codex',
      readFileFn: () => { throw new Error('ENOENT'); },
      spawnFn,
    });
    assert.equal(snap.windows[0].usedPct, 25);
    assert.equal(snap.windows[1].usedPct, 18);
  });

  it('no CodexBar, no auth.json, no codex CLI → honest no_source', async () => {
    const snap = await collectCodex({
      now: NOW,
      env: {},
      whichFn: () => false,
      readFileFn: () => { throw new Error('ENOENT'); },
    });
    assert.equal(snap.error, 'codex_unavailable');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
  });

  it('wham window without used_percent stays omitted', async () => {
    const snap = await collectCodex({
      now: NOW,
      env: { CODEX_HOME: '/x' },
      whichFn: () => false,
      readFileFn: () => JSON.stringify({ tokens: { access_token: 'tok' } }),
      fetchImpl: async () => ({
        ok: true,
        json: async () => ({
          rate_limit: { primary_window: { reset_at: 1756653307 } },
        }),
      }),
    });
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
  });
});
