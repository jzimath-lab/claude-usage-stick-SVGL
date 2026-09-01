'use strict';

/**
 * Background collector. One source failing must not take the others down.
 * Poll is independent of whoever is looking at GET /cotas.
 * This slice: Actions via G1, Codex via CodexBar / wham / app-server (ZYN-569).
 * Cursor / Gemini stay no_source. The stick only paints.
 */

const { SOURCES, noSource, iso, emptyPayload } = require('./snapshot');
const { collectActions } = require('./g1');
const { collectCodex, onPath } = require('./codex');

function createCollector({
  pollMs = 90_000,
  nowFn = Date.now,
  collectActionsFn = collectActions,
  collectCodexFn = collectCodex,
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
    } else if (process.env.CODEXBAR_URL) {
      console.log('[cotas] Codex via CODEXBAR_URL GET /usage');
    } else {
      console.log('[cotas] Codex via auth.json + wham/usage (or app-server if `codex` on PATH)');
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
