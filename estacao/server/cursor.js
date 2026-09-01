'use strict';

/**
 * Cursor / Grok Bot collector — station only. The ESP32 never opens
 * state.vscdb or cookie DBs.
 *
 * Preference (spec v1 / ZYN-570):
 *   1. CURSOR_FIXTURE        — tests only
 *   2. CODEXBAR_URL          — GET /usage?provider=cursor
 *   3. `codexbar` on PATH    — `codexbar usage --format json --provider cursor`
 *   4. Closed list:
 *        cookie  — CURSOR_COOKIE (pasted). Linux never auto-imports a browser.
 *        token   — CURSOR_TOKEN, or cursorAuth/accessToken in state.vscdb
 *                  (macOS default path; CURSOR_VSCDB override). Not on Linux
 *                  unless the path is set explicitly.
 *        GET  https://cursor.com/api/usage-summary
 *        POST https://cursor.com/api/dashboard/get-sand-usage-status
 *             Origin: https://cursor.com  (best-effort; failure keeps monthly bars)
 *
 * Out of v1: get-filtered-usage-events, screen scrape, Add/Switch Account.
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile, execFileSync } = require('child_process');
const { onPath, parseJsonLoose } = require('./codex');
const {
  iso, noSource,
  mapCursorFromCodexBar, mapCursorFromUsageSummary,
} = require('./snapshot');

const USAGE_SUMMARY_URL = 'https://cursor.com/api/usage-summary';
const SAND_USAGE_URL = 'https://cursor.com/api/dashboard/get-sand-usage-status';
const COOKIE_NAMES = [
  'WorkosCursorSessionToken',
  '__Secure-next-auth.session-token',
  'next-auth.session-token',
];

function serveUsageUrl(base) {
  const u = String(base).trim();
  if (/\/usage(\?|$)/.test(u)) {
    if (/[?&]provider=cursor\b/.test(u)) return u;
    if (/[?&]provider=/.test(u)) {
      return u.replace(/([?&]provider=)[^&]*/, '$1cursor');
    }
    return `${u}${u.includes('?') ? '&' : '?'}provider=cursor`;
  }
  return `${u.replace(/\/$/, '')}/usage?provider=cursor`;
}

function execFileAsync(execFileFn, bin, args, opts) {
  return new Promise((resolve, reject) => {
    execFileFn(bin, args, opts, (err, stdout, stderr) => {
      if (err) {
        const code = err.code;
        if (code === 'ETIMEDOUT' || code === 'TIMEOUT') return reject(new Error('codexbar_timeout'));
        if (code === 'ENOENT') return reject(new Error('codexbar_missing'));
        return reject(new Error('codexbar_error'));
      }
      resolve({ stdout: stdout == null ? '' : String(stdout), stderr });
    });
  });
}

function defaultVscdbPath(platform = process.platform, env = process.env, homedirFn = os.homedir) {
  if (env.CURSOR_VSCDB) return env.CURSOR_VSCDB;
  // Linux = pasted cookie. Do not walk ~/.config/Cursor on our own.
  if (platform === 'linux') return null;
  const home = homedirFn();
  if (platform === 'darwin') {
    return path.join(home, 'Library', 'Application Support', 'Cursor', 'User', 'globalStorage', 'state.vscdb');
  }
  if (platform === 'win32') {
    const appdata = env.APPDATA || path.join(home, 'AppData', 'Roaming');
    return path.join(appdata, 'Cursor', 'User', 'globalStorage', 'state.vscdb');
  }
  return null;
}

/** JWT payload. Never log. */
function jwtPayload(token) {
  if (typeof token !== 'string') return null;
  const parts = token.split('.');
  if (parts.length < 2) return null;
  try {
    let b64 = parts[1].replace(/-/g, '+').replace(/_/g, '/');
    b64 += '='.repeat((4 - (b64.length % 4)) % 4);
    return JSON.parse(Buffer.from(b64, 'base64').toString('utf8'));
  } catch {
    return null;
  }
}

function tokenUsable(token, nowMs = Date.now()) {
  const p = jwtPayload(token);
  if (!p) return !!token;
  if (typeof p.exp === 'number' && p.exp * 1000 <= nowMs + 60_000) return false;
  return true;
}

function userIdFromToken(token) {
  const p = jwtPayload(token);
  const sub = p && typeof p.sub === 'string' ? p.sub : '';
  const id = sub.split('|').filter(Boolean).pop();
  return id || null;
}

function cookieFromAccessToken(token) {
  const id = userIdFromToken(token);
  if (!id) return `WorkosCursorSessionToken=${token}`;
  return `WorkosCursorSessionToken=${id}%3A%3A${token}`;
}

function normalizeCookieHeader(raw) {
  if (raw == null) return '';
  let s = String(raw).trim();
  if (!s) return '';
  if (/^cookie:\s*/i.test(s)) s = s.replace(/^cookie:\s*/i, '').trim();
  if (COOKIE_NAMES.some((n) => s.includes(`${n}=`))) return s;
  return `WorkosCursorSessionToken=${s}`;
}

function decodeVscdbValue(raw) {
  if (raw == null) return '';
  let s = Buffer.isBuffer(raw) ? raw.toString('utf8') : String(raw);
  if (s.includes('\u0000')) s = s.replace(/\u0000/g, '');
  return s.trim();
}

function readAccessTokenFromVscdb({
  dbPath,
  execFileFn = execFileSync,
  nowMs = Date.now(),
} = {}) {
  if (!dbPath) return null;
  try {
    if (!fs.existsSync(dbPath)) return null;
  } catch {
    return null;
  }
  let stdout;
  try {
    stdout = execFileFn(
      'sqlite3',
      ['-readonly', dbPath, "SELECT value FROM ItemTable WHERE key = 'cursorAuth/accessToken' LIMIT 1;"],
      { encoding: 'utf8', timeout: 4000, stdio: ['ignore', 'pipe', 'ignore'] },
    );
  } catch {
    return null;
  }
  const token = decodeVscdbValue(stdout);
  if (!token || !tokenUsable(token, nowMs)) return null;
  return token;
}

function resolveSession({
  env = process.env,
  platform = process.platform,
  homedirFn = os.homedir,
  execFileFn = execFileSync,
  nowMs = Date.now(),
} = {}) {
  const pasted = normalizeCookieHeader(env.CURSOR_COOKIE);
  if (pasted) return { cookie: pasted, via: 'cookie' };

  if (env.CURSOR_TOKEN && tokenUsable(env.CURSOR_TOKEN, nowMs)) {
    return { cookie: cookieFromAccessToken(env.CURSOR_TOKEN), via: 'token' };
  }

  const dbPath = defaultVscdbPath(platform, env, homedirFn);
  const token = readAccessTokenFromVscdb({ dbPath, execFileFn, nowMs });
  if (token) return { cookie: cookieFromAccessToken(token), via: 'vscdb' };

  return null;
}

function cursorHeaders(cookie, extra) {
  return {
    Accept: 'application/json',
    Cookie: cookie,
    ...extra,
  };
}

async function collectViaServe({ url, token, now, fetchImpl }) {
  const headers = { Accept: 'application/json' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const r = await fetchImpl(url, { headers, signal: AbortSignal.timeout(20000) });
  if (!r.ok) throw new Error(`codexbar_http_${r.status}`);
  return mapCursorFromCodexBar(await r.json(), now);
}

async function collectViaCli({ now, execFileFn, env }) {
  const { stdout } = await execFileAsync(
    execFileFn,
    'codexbar',
    ['usage', '--format', 'json', '--provider', 'cursor'],
    {
      timeout: 20000,
      maxBuffer: 2 * 1024 * 1024,
      encoding: 'utf8',
      env: { ...process.env, ...env },
    },
  );
  return mapCursorFromCodexBar(parseJsonLoose(stdout), now);
}

async function fetchUsageSummary({ cookie, url, fetchImpl }) {
  const r = await fetchImpl(url, {
    headers: cursorHeaders(cookie),
    signal: AbortSignal.timeout(15000),
  });
  if (!r.ok) throw new Error(`usage_summary_http_${r.status}`);
  return r.json();
}

async function fetchSandUsage({ cookie, url, fetchImpl }) {
  const r = await fetchImpl(url, {
    method: 'POST',
    headers: cursorHeaders(cookie, {
      'Content-Type': 'application/json',
      Origin: 'https://cursor.com',
    }),
    body: '{}',
    signal: AbortSignal.timeout(8000),
  });
  if (!r.ok) throw new Error(`sand_http_${r.status}`);
  return r.json();
}

async function collectViaClosedList({
  now, env, platform, fetchImpl, homedirFn, execFileFn,
}) {
  const session = resolveSession({ env, platform, homedirFn, execFileFn, nowMs: now });
  if (!session) {
    throw new Error(platform === 'linux' ? 'cursor_cookie_required' : 'cursor_unavailable');
  }
  const summaryUrl = env.CURSOR_USAGE_URL || USAGE_SUMMARY_URL;
  const sandUrl = env.CURSOR_SAND_URL || SAND_USAGE_URL;
  const summary = await fetchUsageSummary({ cookie: session.cookie, url: summaryUrl, fetchImpl });
  let sand;
  try {
    sand = await fetchSandUsage({ cookie: session.cookie, url: sandUrl, fetchImpl });
  } catch {
    sand = undefined; // POST failure must not zero monthly bars
  }
  const snap = mapCursorFromUsageSummary(summary, now, sand);
  snap.via = session.via;
  return snap;
}

async function collectCursor({
  now = Date.now(),
  env = process.env,
  platform = process.platform,
  fetchImpl = fetch,
  execFileFn = execFile,
  sqliteExecFn = execFileSync,
  whichFn = onPath,
  readFileFn = fs.readFileSync,
  homedirFn = os.homedir,
} = {}) {
  const asOf = iso(now);
  let lastErr = '';
  try {
    if (env.CURSOR_FIXTURE) {
      return mapCursorFromCodexBar(JSON.parse(readFileFn(env.CURSOR_FIXTURE, 'utf8')), now);
    }

    if (env.CODEXBAR_URL) {
      try {
        return await collectViaServe({
          url: serveUsageUrl(env.CODEXBAR_URL),
          token: env.CODEXBAR_TOKEN,
          now,
          fetchImpl,
        });
      } catch (e) {
        lastErr = e.message || 'codexbar_serve';
      }
    }

    if (whichFn('codexbar', env)) {
      try {
        return await collectViaCli({ now, execFileFn, env });
      } catch (e) {
        lastErr = e.message || 'codexbar_cli';
      }
    }

    try {
      return await collectViaClosedList({
        now, env, platform, fetchImpl, homedirFn, execFileFn: sqliteExecFn,
      });
    } catch (e) {
      lastErr = e.message || lastErr || 'cursor_error';
    }

    return noSource('cursor', asOf, lastErr || 'cursor_unavailable');
  } catch (e) {
    return noSource('cursor', asOf, e.message || 'cursor_error');
  }
}

module.exports = {
  collectCursor,
  serveUsageUrl,
  defaultVscdbPath,
  normalizeCookieHeader,
  cookieFromAccessToken,
  resolveSession,
  decodeVscdbValue,
  USAGE_SUMMARY_URL,
  SAND_USAGE_URL,
};
