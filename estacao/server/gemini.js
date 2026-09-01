'use strict';

/**
 * Gemini collector — station only. The ESP32 never opens oauth_creds.json.
 *
 * Preference (spec v1 / ZYN-572):
 *   1. GEMINI_FIXTURE        — tests only
 *   2. `codexbar` on PATH    — `codexbar usage --format json --provider gemini`
 *   3. CODEXBAR_URL          — GET /usage?provider=gemini
 *   4. Closed list:
 *        ~/.gemini/oauth_creds.json (GEMINI_CREDS override)
 *        POST https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota
 *        body `{}`  Authorization: Bearer <access_token>
 *
 * Probe before painting a number. Consumer OAuth (individual / AI Pro / Ultra)
 * may be dead since 2026-06-18 → SEM FONTE (`consumer_shutdown`), never 0%,
 * never Antigravity. Workspace / Standard / Enterprise still use this path.
 *
 * Out of v1: loadCodeAssist, Cloud Billing, AI Studio scrape, Antigravity.
 * Tokens stay in memory; we never write them back to oauth_creds.json
 * and never log them.
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile } = require('child_process');
const { onPath, parseJsonLoose } = require('./codex');
const {
  iso, noSource, hasSourcedUsage, isConsumerShutdown,
  mapGeminiFromCodexBar, mapGeminiFromQuota,
} = require('./snapshot');

const QUOTA_URL = 'https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota';

function credsPath(env = process.env, homedirFn = os.homedir) {
  if (env.GEMINI_CREDS) return env.GEMINI_CREDS;
  return path.join(homedirFn(), '.gemini', 'oauth_creds.json');
}

/** Read access_token. Never log the result. Never write the file. */
function parseOAuthCreds(raw) {
  let json;
  try { json = JSON.parse(raw); } catch { return null; }
  if (!json || typeof json !== 'object') return null;
  const accessToken = json.access_token || json.accessToken;
  if (typeof accessToken !== 'string' || !accessToken) return null;
  return { accessToken };
}

function readCreds(env = process.env, readFileFn = fs.readFileSync, homedirFn = os.homedir) {
  const file = credsPath(env, homedirFn);
  try {
    return parseOAuthCreds(readFileFn(file, 'utf8'));
  } catch {
    return null;
  }
}

function serveUsageUrl(base) {
  const u = String(base).trim();
  if (/\/usage(\?|$)/.test(u)) {
    if (/[?&]provider=gemini\b/.test(u)) return u;
    if (/[?&]provider=/.test(u)) {
      return u.replace(/([?&]provider=)[^&]*/, '$1gemini');
    }
    return `${u}${u.includes('?') ? '&' : '?'}provider=gemini`;
  }
  return `${u.replace(/\/$/, '')}/usage?provider=gemini`;
}

function execFileAsync(execFileFn, bin, args, opts) {
  return new Promise((resolve, reject) => {
    execFileFn(bin, args, opts, (err, stdout, stderr) => {
      if (err) {
        const code = err.code;
        if (code === 'ETIMEDOUT' || code === 'TIMEOUT') return reject(new Error('codexbar_timeout'));
        if (code === 'ENOENT') return reject(new Error('codexbar_missing'));
        const blob = `${stdout || ''} ${stderr || ''} ${err.message || ''}`;
        if (isConsumerShutdown(blob)) return reject(new Error('consumer_shutdown'));
        return reject(new Error('codexbar_error'));
      }
      resolve({ stdout: stdout == null ? '' : String(stdout), stderr });
    });
  });
}

async function collectViaServe({ url, token, now, fetchImpl }) {
  const headers = { Accept: 'application/json' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const r = await fetchImpl(url, { headers, signal: AbortSignal.timeout(20000) });
  if (!r.ok) {
    const body = await readBodyQuiet(r);
    if (isConsumerShutdown(body)) throw new Error('consumer_shutdown');
    throw new Error(`codexbar_http_${r.status}`);
  }
  return mapGeminiFromCodexBar(await r.json(), now);
}

async function collectViaCli({ now, execFileFn, env }) {
  const { stdout } = await execFileAsync(
    execFileFn,
    'codexbar',
    ['usage', '--format', 'json', '--provider', 'gemini'],
    {
      timeout: 20000,
      maxBuffer: 2 * 1024 * 1024,
      encoding: 'utf8',
      env: { ...process.env, ...env },
    },
  );
  return mapGeminiFromCodexBar(parseJsonLoose(stdout), now);
}

async function readBodyQuiet(r) {
  if (typeof r.json === 'function') {
    try { return await r.json(); } catch { return null; }
  }
  if (typeof r.text === 'function') {
    try { return await r.text(); } catch { return null; }
  }
  return null;
}

async function collectViaQuota({ creds, now, env, fetchImpl }) {
  const url = env.GEMINI_QUOTA_URL || QUOTA_URL;
  const r = await fetchImpl(url, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${creds.accessToken}`,
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: '{}',
    signal: AbortSignal.timeout(15000),
  });
  const body = await readBodyQuiet(r);
  if (isConsumerShutdown(body)) {
    throw new Error('consumer_shutdown');
  }
  if (!r.ok) {
    throw new Error(`quota_http_${r.status}`);
  }
  return mapGeminiFromQuota(body, now);
}

async function collectGemini({
  now = Date.now(),
  env = process.env,
  fetchImpl = fetch,
  execFileFn = execFile,
  whichFn = onPath,
  readFileFn = fs.readFileSync,
  homedirFn = os.homedir,
} = {}) {
  const asOf = iso(now);
  let lastErr = '';
  try {
    if (env.GEMINI_FIXTURE) {
      return mapGeminiFromCodexBar(JSON.parse(readFileFn(env.GEMINI_FIXTURE, 'utf8')), now);
    }

    if (whichFn('codexbar', env)) {
      try {
        const snap = await collectViaCli({ now, execFileFn, env });
        if (snap.error === 'consumer_shutdown') return snap;
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'codexbar_cli_no_source';
      } catch (e) {
        lastErr = e.message || 'codexbar_cli';
        if (lastErr === 'consumer_shutdown') {
          return noSource('gemini', asOf, 'consumer_shutdown');
        }
      }
    }

    if (env.CODEXBAR_URL) {
      try {
        const snap = await collectViaServe({
          url: serveUsageUrl(env.CODEXBAR_URL),
          token: env.CODEXBAR_TOKEN,
          now,
          fetchImpl,
        });
        if (snap.error === 'consumer_shutdown') return snap;
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'codexbar_no_source';
      } catch (e) {
        lastErr = e.message || 'codexbar_serve';
        if (lastErr === 'consumer_shutdown') {
          return noSource('gemini', asOf, 'consumer_shutdown');
        }
      }
    }

    const creds = readCreds(env, readFileFn, homedirFn);
    if (!creds) {
      return noSource('gemini', asOf, lastErr || 'gemini_unavailable');
    }

    try {
      const snap = await collectViaQuota({ creds, now, env, fetchImpl });
      if (snap.error === 'consumer_shutdown') return snap;
      if (hasSourcedUsage(snap)) return snap;
      lastErr = snap.error || lastErr || 'quota_no_source';
    } catch (e) {
      lastErr = e.message || lastErr || 'quota_error';
    }

    return noSource('gemini', asOf, lastErr || 'gemini_unavailable');
  } catch (e) {
    const msg = e.message || 'gemini_error';
    return noSource('gemini', asOf, msg);
  }
}

module.exports = {
  collectGemini,
  serveUsageUrl,
  credsPath,
  parseOAuthCreds,
  QUOTA_URL,
};
