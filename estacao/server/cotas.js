'use strict';

/**
 * Background collector. One source failing must not take the others down.
 * Poll is independent of whoever is looking at GET /cotas.
 * This slice: Actions, Codex, Cursor / Grok Bot, Gemini probe (ZYN-572).
 * The stick only paints.
 */

const { SOURCES, noSource, iso, emptyPayload } = require('./snapshot');
const { collectGithubBilling: collectActions } = require('./github-billing');
const { collectCodex, onPath } = require('./codex');
const { collectClaude } = require('./claude');
const { collectCursor } = require('./cursor');
const { collectGemini } = require('./gemini');

function createCollector({
  pollMs = 90_000,
  nowFn = Date.now,
  collectActionsFn = collectActions,
  collectClaudeFn = collectClaude,
  collectCodexFn = collectCodex,
  collectCursorFn = collectCursor,
  collectGeminiFn = collectGemini,
  history = null,
} = {}) {
  const cache = new Map();
  const errors = {};
  let timer = null;
  let busy = new Set();

  function asOf() {
    return iso(nowFn());
  }

  function seed() {
    const t = asOf();
    for (const id of SOURCES) {
      if (!cache.has(id)) cache.set(id, noSource(id, t));
    }
  }

  async function refreshOne(id) {
    if (busy.has(id)) return;
    busy.add(id);
    try {
      let snap = null;
      if (id === 'claude') snap = await collectClaudeFn({ now: nowFn() });
      if (id === 'actions') snap = await collectActionsFn({ now: nowFn() });
      else if (id === 'codex') snap = await collectCodexFn({ now: nowFn() });
      else if (id === 'cursor') snap = await collectCursorFn({ now: nowFn() });
      else if (id === 'gemini') snap = await collectGeminiFn({ now: nowFn() });
      if (snap) {
        if (history) snap = history.enrich(snap);
        cache.set(id, snap);
        if (snap.error) errors[id] = snap.error;
        else delete errors[id];
      }
    } catch (e) {
      errors[id] = e.message || String(e);
      const prev = cache.get(id);
      if (!prev || prev.windows.every((w) => w.status === 'no_source')) {
        cache.set(id, noSource(id, asOf(), errors[id]));
      }
    } finally {
      busy.delete(id);
    }
  }

  async function refreshAll() {
    await Promise.allSettled(SOURCES.map((id) => refreshOne(id)));
  }

  function payload() {
    seed();
    return {
      asOf: asOf(),
      sources: SOURCES.map((id) => cache.get(id) || noSource(id, asOf(), errors[id])),
    };
  }

  function start() {
    seed();
    if (onPath('codexbar')) {
      console.log('[cotas] Claude via `codexbar dashboard` (sessionKey no app CodexBar; flap degrada para stale)');
      console.log('[cotas] Codex via `codexbar usage --format json --provider codex` (then serve / wham / app-server)');
      console.log('[cotas] Cursor via `codexbar usage --format json --provider cursor` (CLI before CODEXBAR_URL)');
      if (process.env.GEMINI_PRODUCT !== 'app') console.log('[cotas] Gemini via `codexbar usage --format json --provider gemini` (then serve / retrieveUserQuota)');
    } else if (process.env.CODEXBAR_URL) {
      console.log('[cotas] Codex / Cursor / Gemini via CODEXBAR_URL GET /usage');
    } else {
      console.log('[cotas] Codex via auth.json + wham/usage (or app-server if `codex` on PATH)');
      if (process.env.CURSOR_COOKIE || process.env.CURSOR_TOKEN || process.env.CURSOR_VSCDB) {
        console.log('[cotas] Cursor via usage-summary + sand-usage (pasted cookie / token / vscdb)');
      } else if (process.platform === 'linux') {
        console.log('[cotas] Cursor: paste CURSOR_COOKIE (Linux does not import a browser)');
      } else {
        console.log('[cotas] Cursor via state.vscdb or CURSOR_COOKIE → usage-summary');
      }
      if (process.env.GEMINI_PRODUCT !== 'app') console.log('[cotas] Gemini via ~/.gemini/oauth_creds.json → retrieveUserQuota (no loadCodeAssist)');
    }
    if (process.env.GEMINI_PRODUCT === 'app') console.log('[cotas] Gemini Apps via extensao Chrome / ingest local autenticado');
    refreshAll().catch((e) => console.warn('[cotas] first poll:', e.message));
    timer = setInterval(() => {
      refreshAll().catch((e) => console.warn('[cotas] poll:', e.message));
    }, pollMs);
    if (timer.unref) timer.unref();
  }

  function stop() {
    if (timer) clearInterval(timer);
    timer = null;
  }

  return { payload, refreshAll, refreshOne, start, stop, cache, errors };
}

function firstPayload(nowMs) {
  return emptyPayload(nowMs);
}

module.exports = { createCollector, firstPayload };
