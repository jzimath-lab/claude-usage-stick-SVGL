'use strict';

/**
 * Claude collector — the source the ZYN-568 migration left behind (found
 * 2026-09-17: `claude` sat in SOURCES as a permanent no_source stub).
 *
 * Single strategy, deliberately: `codexbar dashboard`. The sessionKey lives in
 * the CodexBar app config and never touches this process or the ESP32. See
 * mapClaudeFromDashboard for why `usage --provider claude` does not work.
 *
 * Two contracts that came from measuring, not taste:
 *  - THROW on failure. The web source flaps (3 windows at 18:18, cli error at
 *    18:44 the same day, cookie near end of its ~12-day life). refreshOne()'s
 *    catch keeps the last good snapshot; returning noSource would replace it.
 *    Flap must degrade to stale, never to blank.
 *  - Throttle SUCCESSES only. Dashboard probes every provider (~30s); running
 *    it each 90s poll is waste. But a failure must not arm the throttle —
 *    the next poll retries.
 */

const { execFile } = require('child_process');
const { onPath } = require('./codex');
const { mapClaudeFromDashboard, hasSourcedUsage } = require('./snapshot');

const MIN_MS = 240_000;           // dashboard no more often than this
const g_state = { at: 0 };        // module-level: survives poll cycles

function execFileAsync(execFileFn, bin, args, opts) {
  return new Promise((resolve, reject) => {
    execFileFn(bin, args, opts, (err, stdout) => {
      if (err) {
        if (err.code === 'ETIMEDOUT' || err.code === 'TIMEOUT') return reject(new Error('dashboard_timeout'));
        if (err.code === 'ENOENT') return reject(new Error('codexbar_missing'));
        return reject(new Error('dashboard_error'));
      }
      resolve(stdout);
    });
  });
}

async function collectClaude({
  now = Date.now(),
  env = process.env,
  execFileFn = execFile,
  whichFn = onPath,
  minMs = MIN_MS,
  state = g_state,
} = {}) {
  if (state.at && now - state.at < minMs) return null;   // null = keep cache
  if (!whichFn('codexbar', env)) throw new Error('codexbar_missing');

  const out = await execFileAsync(execFileFn, 'codexbar',
    ['dashboard', '--identity', 'redacted', '--timeout', '45'],
    { timeout: 60_000, maxBuffer: 4 * 1024 * 1024, env });

  let doc;
  try { doc = JSON.parse(out); } catch { throw new Error('dashboard_parse'); }

  const snap = mapClaudeFromDashboard(doc, now);
  if (!hasSourcedUsage(snap)) throw new Error(snap.error || 'claude_no_usage');

  state.at = now;                 // successes throttle; failures retry
  return snap;
}

module.exports = { collectClaude, MIN_MS };
