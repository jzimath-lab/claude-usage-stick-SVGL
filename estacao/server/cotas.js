'use strict';

/**
 * Background collector. One source failing must not take the others down.
 * Poll is independent of whoever is looking at GET /cotas.
 * This slice: Actions via G1, Codex via CodexBar / wham / app-server,
 * Cursor / Grok Bot via CodexBar / usage-summary + sand-usage (ZYN-570).
 * Gemini stays no_source. The stick only paints.
 */

const { SOURCES, noSource, iso, emptyPayload } = require('./snapshot');
const { collectActions } = require('./g1');
const { collectCodex, onPath } = require('./codex');
const { collectCursor } = require('./cursor');

function createCollector({
  pollMs = 90_000,
  nowFn = Date.now,
  collectActionsFn = collectActions,
  collectCodexFn = collectCodex,
  collectCursorFn = collectCursor,
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
      if (id === 'actions') snap = await collectActionsFn({ now: nowFn() });
      else if (id === 'codex') snap = await collectCodexFn({ now: nowFn() });
      else if (id === 'cursor') snap = await collectCursorFn({ now: nowFn() });
      if (snap) {
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
      console.log('[cotas] Codex via `codexbar usage --format json --provider codex`');
      console.log('[cotas] Cursor via `codexbar usage --format json --provider cursor`');
    } else if (process.env.CODEXBAR_URL) {
      console.log('[cotas] Codex / Cursor via CODEXBAR_URL GET /usage');
    } else {
      console.log('[cotas] Codex via auth.json + wham/usage (or app-server if `codex` on PATH)');
      if (process.env.CURSOR_COOKIE || process.env.CURSOR_TOKEN || process.env.CURSOR_VSCDB) {
        console.log('[cotas] Cursor via usage-summary + sand-usage (pasted cookie / token / vscdb)');
      } else if (process.platform === 'linux') {
        console.log('[cotas] Cursor: paste CURSOR_COOKIE (Linux does not import a browser)');
      } else {
        console.log('[cotas] Cursor via state.vscdb or CURSOR_COOKIE → usage-summary');
      }
    }
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
