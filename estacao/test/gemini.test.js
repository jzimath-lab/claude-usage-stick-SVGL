'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const {
  collectGemini, serveUsageUrl, credsPath, parseOAuthCreds, QUOTA_URL,
} = require('../server/gemini');

const NOW = Date.parse('2026-08-31T12:00:00.000Z');

describe('serveUsageUrl', () => {
  it('forces provider=gemini', () => {
    assert.equal(serveUsageUrl('http://127.0.0.1:8080'),
      'http://127.0.0.1:8080/usage?provider=gemini');
    assert.equal(serveUsageUrl('http://127.0.0.1:8080/usage?provider=codex'),
      'http://127.0.0.1:8080/usage?provider=gemini');
  });
});

describe('credsPath / parseOAuthCreds', () => {
  it('defaults to ~/.gemini/oauth_creds.json', () => {
    assert.equal(
      credsPath({}, () => '/home/you'),
      path.join('/home/you', '.gemini', 'oauth_creds.json'),
    );
    assert.equal(
      credsPath({ GEMINI_CREDS: '/tmp/oauth_creds.json' }, () => '/home/you'),
      '/tmp/oauth_creds.json',
    );
  });
  it('requires access_token and ignores junk', () => {
    assert.equal(parseOAuthCreds(JSON.stringify({ access_token: 'tok' })).accessToken, 'tok');
    assert.equal(parseOAuthCreds('{"refresh_token":"only"}'), null);
    assert.equal(parseOAuthCreds('not-json'), null);
  });
});

describe('collectGemini', () => {
  it('GEMINI_FIXTURE maps CodexBar JSON (tests only)', async () => {
    const snap = await collectGemini({
      now: NOW,
      env: { GEMINI_FIXTURE: path.join(__dirname, 'fixtures/gemini-usage.json') },
      whichFn: () => false,
    });
    assert.equal(snap.windows[0].name, 'hoje');
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(snap.windows[0].resetAt, '2026-09-01T07:00:00.000Z');
    assert.equal(snap.windows[1].name, 'ciclo');
    assert.equal(snap.windows[1].usedPct, 15);
  });

  it('codexbar CLI is preferred and does not open oauth_creds.json', async () => {
    let readCreds = false;
    const snap = await collectGemini({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        assert.equal(bin, 'codexbar');
        assert.deepEqual(args, ['usage', '--format', 'json', '--provider', 'gemini']);
        cb(null, JSON.stringify({
          provider: 'gemini',
          usage: {
            primary: { usedPercent: 42, resetsAt: '2026-09-01T07:00:00.000Z' },
            secondary: { usedPercent: 15 },
          },
        }));
      },
      readFileFn: () => { readCreds = true; throw new Error('oauth_creds.json should stay closed'); },
      fetchImpl: async () => { throw new Error('quota must not run'); },
    });
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(readCreds, false);
  });

  it('CLI no_source continues to retrieveUserQuota', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gemini-creds-'));
    const file = path.join(dir, 'oauth_creds.json');
    fs.writeFileSync(file, JSON.stringify({ access_token: 'tok' }));
    const snap = await collectGemini({
      now: NOW,
      env: { GEMINI_CREDS: file, GEMINI_QUOTA_URL: QUOTA_URL },
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({ provider: 'gemini', error: 'not logged in' }));
      },
      fetchImpl: async (url, opts) => {
        assert.equal(url, QUOTA_URL);
        assert.equal(opts.method, 'POST');
        assert.equal(opts.body, '{}');
        assert.equal(opts.headers.Authorization, 'Bearer tok');
        return {
          ok: true,
          json: async () => ({
            buckets: [
              { modelId: 'gemini-2.5-pro', remainingFraction: 0.58, resetTime: '2026-09-01T07:00:00.000Z' },
            ],
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 42);
    fs.rmSync(dir, { recursive: true });
  });

  it('CLI consumer shutdown does not open oauth_creds.json and never paints 0%', async () => {
    let readCreds = false;
    const snap = await collectGemini({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({
          provider: 'gemini',
          error: 'UNSUPPORTED_CLIENT: migrate to Antigravity for Gemini CLI',
        }));
      },
      readFileFn: () => { readCreds = true; throw new Error('must not open oauth_creds after shutdown'); },
      fetchImpl: async () => { throw new Error('must not pivot to Antigravity or quota'); },
    });
    assert.equal(snap.error, 'consumer_shutdown');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(readCreds, false);
  });

  it('CLI measured 0% does not fall through to oauth_creds', async () => {
    let readCreds = false;
    const snap = await collectGemini({
      now: NOW,
      env: {},
      whichFn: (bin) => bin === 'codexbar',
      execFileFn: (bin, args, opts, cb) => {
        cb(null, JSON.stringify({
          provider: 'gemini',
          usage: { primary: { usedPercent: 0 }, secondary: { usedPercent: 0 } },
        }));
      },
      readFileFn: () => { readCreds = true; throw new Error('oauth_creds.json should stay closed'); },
    });
    assert.equal(snap.windows[0].usedPct, 0);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(readCreds, false);
  });

  it('CODEXBAR_URL GET /usage when CLI is absent does not open oauth_creds', async () => {
    let readCreds = false;
    const snap = await collectGemini({
      now: NOW,
      env: { CODEXBAR_URL: 'http://127.0.0.1:8080' },
      whichFn: () => false,
      readFileFn: () => { readCreds = true; throw new Error('oauth_creds.json should stay closed'); },
      fetchImpl: async (url) => {
        assert.equal(url, 'http://127.0.0.1:8080/usage?provider=gemini');
        return {
          ok: true,
          json: async () => ({
            provider: 'gemini',
            usage: { primary: { usedPercent: 17 } },
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 17);
    assert.equal(readCreds, false);
  });

  it('oauth_creds + retrieveUserQuota when CodexBar is absent', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gemini-creds-'));
    const file = path.join(dir, 'oauth_creds.json');
    fs.writeFileSync(file, JSON.stringify({ access_token: 'tok', refresh_token: 'ref' }));
    const snap = await collectGemini({
      now: NOW,
      env: { GEMINI_CREDS: file },
      whichFn: () => false,
      fetchImpl: async (url, opts) => {
        assert.equal(url, QUOTA_URL);
        assert.equal(opts.method, 'POST');
        assert.equal(opts.body, '{}');
        assert.equal(opts.headers.Authorization, 'Bearer tok');
        return {
          ok: true,
          json: async () => ({
            buckets: [
              { modelId: 'gemini-2.5-pro', remainingFraction: 0.58, resetTime: '2026-09-01T07:00:00.000Z' },
              { modelId: 'gemini-2.5-flash', remainingFraction: 0.85, resetTime: '2026-09-01T07:00:00.000Z' },
            ],
          }),
        };
      },
    });
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(snap.windows[1].usedPct, 15);
    fs.rmSync(dir, { recursive: true });
  });

  it('quota 403 SUBSCRIPTION_REQUIRED → consumer_shutdown, never 0; token is not in the error', async () => {
    const snap = await collectGemini({
      now: NOW,
      env: { GEMINI_CREDS: '/tmp/oauth_creds.json' },
      whichFn: () => false,
      readFileFn: () => JSON.stringify({ access_token: 'super-secret-gemini-token' }),
      fetchImpl: async () => ({
        ok: false,
        status: 403,
        json: async () => ({ error: { status: 'SUBSCRIPTION_REQUIRED', code: 403 } }),
      }),
    });
    assert.equal(snap.error, 'consumer_shutdown');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(String(snap.error).includes('super-secret-gemini-token'), false);
  });

  it('quota 401 → no_source, not 0; token is not in the error', async () => {
    const snap = await collectGemini({
      now: NOW,
      env: { GEMINI_CREDS: '/tmp/oauth_creds.json' },
      whichFn: () => false,
      readFileFn: () => JSON.stringify({ access_token: 'super-secret-gemini-token' }),
      fetchImpl: async () => ({ ok: false, status: 401, json: async () => ({ error: 'invalid' }) }),
    });
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.match(snap.error, /quota_http_401/);
    assert.equal(String(snap.error).includes('super-secret-gemini-token'), false);
  });

  it('no CodexBar, no oauth_creds.json → honest no_source', async () => {
    const snap = await collectGemini({
      now: NOW,
      env: {},
      whichFn: () => false,
      homedirFn: () => '/no-such-home',
      readFileFn: () => { throw new Error('ENOENT'); },
    });
    assert.equal(snap.error, 'gemini_unavailable');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
  });

  it('does not call loadCodeAssist or Antigravity', async () => {
    const urls = [];
    await collectGemini({
      now: NOW,
      env: { GEMINI_CREDS: '/tmp/oauth_creds.json' },
      whichFn: () => false,
      readFileFn: () => JSON.stringify({ access_token: 'tok' }),
      fetchImpl: async (url) => {
        urls.push(String(url));
        return {
          ok: true,
          json: async () => ({
            buckets: [{ modelId: 'gemini-2.5-pro', remainingFraction: 1 }],
          }),
        };
      },
    });
    assert.deepEqual(urls, [QUOTA_URL]);
    assert.equal(urls.some((u) => /loadCodeAssist|antigravity|generativelanguage/i.test(u)), false);
  });
});
