'use strict';

/**
 * Codex collector — station only. The ESP32 never opens auth.json.
 *
 * Preference (spec v1 / ZYN-569 + ZYN-574):
 *   1. CODEXBAR_URL          — GET /usage?provider=codex (existing `codexbar serve`)
 *   2. `codexbar` on PATH    — `codexbar usage --format json --provider codex`
 *   3. ~/.codex/auth.json (or $CODEX_HOME/auth.json)
 *        → GET https://chatgpt.com/backend-api/wham/usage
 *   4. `codex` on PATH       — RPC `codex -s read-only -a never app-server`
 *        → initialize + account/rateLimits/read
 *
 * HTTP 200 / CLI exit 0 that maps to no_source does not return immediately —
 * continue the chain. Return a preferred snapshot only when some window has
 * sourced usage (usedPct present, including measured 0). Stick never opens
 * auth.json.
 *
 * Closed list. No chatgpt.com scrape / WKWebView. Tokens stay in memory;
 * we never write them back to auth.json and never log them.
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile, execFileSync, spawn } = require('child_process');
const {
  iso, noSource, hasSourcedUsage,
  mapCodexFromCodexBar, mapCodexFromWham, mapCodexFromAppServer,
  recoverWhamFromText,
} = require('./snapshot');

const WHAM_URL = 'https://chatgpt.com/backend-api/wham/usage';

function onPath(bin, env = process.env) {
  try {
    execFileSync('which', [bin], { stdio: 'ignore', env: { ...process.env, ...env } });
    return true;
  } catch {
    return false;
  }
}

function authJsonPath(env = process.env, homedirFn = os.homedir) {
  if (env.CODEX_HOME) return path.join(env.CODEX_HOME, 'auth.json');
  return path.join(homedirFn(), '.codex', 'auth.json');
}

/** Read bearer + optional account id. Never log the result. */
function parseAuthJson(raw) {
  let json;
  try { json = JSON.parse(raw); } catch { return null; }
  if (!json || typeof json !== 'object') return null;
  const tokens = json.tokens && typeof json.tokens === 'object' ? json.tokens : {};
  const accessToken = tokens.access_token || json.access_token;
  if (typeof accessToken !== 'string' || !accessToken) return null;
  const accountId = tokens.account_id || json.account_id;
  return {
    accessToken,
    accountId: typeof accountId === 'string' && accountId ? accountId : undefined,
  };
}

function readAuth(env = process.env, readFileFn = fs.readFileSync, homedirFn = os.homedir) {
  const file = authJsonPath(env, homedirFn);
  try {
    return parseAuthJson(readFileFn(file, 'utf8'));
  } catch {
    return null;
  }
}

function parseJsonLoose(text) {
  const t = String(text || '').trim();
  try { return JSON.parse(t); } catch { /* fall through */ }
  const i = t.indexOf('{');
  const j = t.lastIndexOf('}');
  if (i >= 0 && j > i) return JSON.parse(t.slice(i, j + 1));
  const a = t.indexOf('[');
  const b = t.lastIndexOf(']');
  if (a >= 0 && b > a) return JSON.parse(t.slice(a, b + 1));
  throw new Error('codexbar_parse');
}

function serveUsageUrl(base) {
  const u = String(base).trim();
  if (/\/usage(\?|$)/.test(u)) {
    return /[?&]provider=/.test(u) ? u : `${u}${u.includes('?') ? '&' : '?'}provider=codex`;
  }
  return `${u.replace(/\/$/, '')}/usage?provider=codex`;
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

async function collectViaServe({ url, token, now, fetchImpl }) {
  const headers = { Accept: 'application/json' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const r = await fetchImpl(url, { headers, signal: AbortSignal.timeout(20000) });
  if (!r.ok) throw new Error(`codexbar_http_${r.status}`);
  return mapCodexFromCodexBar(await r.json(), now);
}

async function collectViaCli({ now, execFileFn, env }) {
  const { stdout } = await execFileAsync(
    execFileFn,
    'codexbar',
    ['usage', '--format', 'json', '--provider', 'codex'],
    {
      timeout: 20000,
      maxBuffer: 2 * 1024 * 1024,
      encoding: 'utf8',
      env: { ...process.env, ...env },
    },
  );
  return mapCodexFromCodexBar(parseJsonLoose(stdout), now);
}

async function collectViaWham({ auth, now, env, fetchImpl }) {
  const url = env.CODEX_WHAM_URL || WHAM_URL;
  const headers = {
    Authorization: `Bearer ${auth.accessToken}`,
    Accept: 'application/json',
  };
  if (auth.accountId) headers['ChatGPT-Account-Id'] = auth.accountId;
  const r = await fetchImpl(url, { headers, signal: AbortSignal.timeout(15000) });
  if (!r.ok) throw new Error(`wham_http_${r.status}`);
  return mapCodexFromWham(await r.json(), now);
}

/**
 * JSON-RPC over stdin/stdout. Newer Codex omits the `"jsonrpc":"2.0"` key.
 * Notifications (no id) are ignored. Process is killed on timeout.
 */
function collectViaAppServer({
  now, env, spawnFn = spawn, timeoutMs = 12000,
}) {
  return new Promise((resolve, reject) => {
    let settled = false;
    let buf = '';
    let initialized = false;
    const child = spawnFn('codex', ['-s', 'read-only', '-a', 'never', 'app-server'], {
      env: { ...process.env, ...env },
      stdio: ['pipe', 'pipe', 'pipe'],
    });

    const timer = setTimeout(() => finish(new Error('app_server_timeout')), timeoutMs);

    function killChild() {
      try { child.kill('SIGTERM'); } catch { /* already gone */ }
      setTimeout(() => { try { child.kill('SIGKILL'); } catch { /* gone */ } }, 400);
    }

    function finish(err, result) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      killChild();
      if (err) reject(err);
      else resolve(result);
    }

    function write(obj) {
      try { child.stdin.write(`${JSON.stringify(obj)}\n`); } catch { /* closed */ }
    }

    function onMessage(msg) {
      if (!msg || typeof msg !== 'object') return;
      if (msg.method && msg.id == null) return; // notification

      if (msg.error && (msg.id === 2 || initialized)) {
        const recovered = recoverWhamFromText(msg.error.message)
          || recoverWhamFromText(msg.error.data)
          || recoverWhamFromText(msg.error);
        if (recovered) {
          finish(null, mapCodexFromWham(recovered, now));
          return;
        }
        finish(new Error('app_server_error'));
        return;
      }

      const result = msg.result;
      if (!result) return;

      if (result.rateLimits || result.rate_limits || result.rate_limit || result.rateLimit) {
        finish(null, mapCodexFromAppServer(result, now));
        return;
      }

      if (!initialized && (msg.id === 1 || result.userAgent || result.codexHome
          || result.capabilities || result.serverInfo)) {
        initialized = true;
        write({ method: 'initialized' });
        write({ id: 2, method: 'account/rateLimits/read', params: {} });
      }
    }

    child.stdout.on('data', (chunk) => {
      buf += chunk.toString();
      const lines = buf.split('\n');
      buf = lines.pop();
      for (const line of lines) {
        const t = line.trim();
        if (!t) continue;
        try { onMessage(JSON.parse(t)); } catch { /* ignore leftover logs */ }
      }
    });
    child.stderr.on('data', () => { /* never log — may echo paths */ });
    child.on('error', (e) => finish(new Error(e.code === 'ENOENT' ? 'codex_missing' : 'app_server_spawn')));
    child.on('exit', () => {
      if (!settled) finish(new Error('app_server_exit'));
    });

    write({
      id: 1,
      method: 'initialize',
      params: {
        clientInfo: { name: 'estacao-cotas', title: 'estacao', version: '1' },
        capabilities: {},
      },
    });
  });
}

async function collectCodex({
  now = Date.now(),
  env = process.env,
  fetchImpl = fetch,
  execFileFn = execFile,
  spawnFn = spawn,
  whichFn = onPath,
  readFileFn = fs.readFileSync,
  homedirFn = os.homedir,
} = {}) {
  const asOf = iso(now);
  let lastErr = '';
  try {
    if (env.CODEX_FIXTURE) {
      return mapCodexFromCodexBar(JSON.parse(readFileFn(env.CODEX_FIXTURE, 'utf8')), now);
    }

    if (env.CODEXBAR_URL) {
      try {
        const snap = await collectViaServe({
          url: serveUsageUrl(env.CODEXBAR_URL),
          token: env.CODEXBAR_TOKEN,
          now,
          fetchImpl,
        });
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'codexbar_no_source';
      } catch (e) {
        lastErr = e.message || 'codexbar_serve';
      }
    }

    if (whichFn('codexbar', env)) {
      try {
        const snap = await collectViaCli({ now, execFileFn, env });
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'codexbar_cli_no_source';
      } catch (e) {
        lastErr = e.message || 'codexbar_cli';
      }
    }

    const auth = readAuth(env, readFileFn, homedirFn);
    if (auth) {
      try {
        const snap = await collectViaWham({ auth, now, env, fetchImpl });
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'wham_no_source';
      } catch (e) {
        lastErr = e.message || 'wham_error';
      }
    }

    if (whichFn('codex', env)) {
      try {
        const snap = await collectViaAppServer({ now, env, spawnFn });
        if (hasSourcedUsage(snap)) return snap;
        lastErr = snap.error || 'app_server_no_source';
      } catch (e) {
        lastErr = e.message || 'app_server_error';
      }
    }

    return noSource('codex', asOf, lastErr || (auth ? 'wham_failed' : 'codex_unavailable'));
  } catch (e) {
    return noSource('codex', asOf, e.message || 'codex_error');
  }
}

module.exports = {
  collectCodex,
  onPath,
  authJsonPath,
  parseAuthJson,
  readAuth,
  serveUsageUrl,
  parseJsonLoose,
  WHAM_URL,
};
