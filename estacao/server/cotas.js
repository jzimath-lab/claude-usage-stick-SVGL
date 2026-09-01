'use strict';

/**
 * Background collector. One source failing must not take the others down.
 * Poll is independent of whoever is looking at GET /cotas.
 * This slice: Actions is real (G1). Everyone else stays no_source (ZYN-569+).
 * CodexBar, if on PATH, is noted but not used — it does not cover Actions.
 */

const { execFileSync } = require('child_process');
const { SOURCES, noSource, iso, emptyPayload } = require('./snapshot');
const { collectActions } = require('./g1');

function codexbarOnPath() {
  try {
    execFileSync('which', ['codexbar'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

function createCollector({ pollMs = 90_000, nowFn = Date.now, collectActionsFn = collectActions } = {}) {
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
      if (id === 'actions') {
        const snap = await collectActionsFn({ now: nowFn() });
        cache.set(id, snap);
        if (snap.error) errors[id] = snap.error;
        else delete errors[id];
      }
      // other sources stay no_source until later issues
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
    if (codexbarOnPath()) {
      console.log('[cotas] codexbar on PATH — unused here (CodexBar does not cover Actions; ZYN-569)');
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

module.exports = { createCollector, firstPayload, codexbarOnPath };
