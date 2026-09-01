'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const {
  noSource, mapActionsFromG1, painelLiteFromBilling, statusFromPct,
  nextCycleResetIso, emptyPayload,
  mapCodexFromCodexBar, mapCodexFromWham, mapCodexFromAppServer,
  recoverWhamFromText,
} = require('../server/snapshot');

describe('statusFromPct', () => {
  it('maps real percentages, including measured 0', () => {
    assert.equal(statusFromPct(0), 'ok');
    assert.equal(statusFromPct(69), 'ok');
    assert.equal(statusFromPct(70), 'warning');
    assert.equal(statusFromPct(100), 'blocked');
    assert.equal(statusFromPct(454), 'blocked'); // G1 July overflow
  });
  it('missing / NaN is no_source, not 0', () => {
    assert.equal(statusFromPct(undefined), 'no_source');
    assert.equal(statusFromPct(NaN), 'no_source');
  });
});

describe('noSource', () => {
  it('omits usedPct and usedAbsolute', () => {
    const s = noSource('codex', '2026-08-31T00:00:00.000Z');
    assert.equal(s.source, 'codex');
    for (const w of s.windows) {
      assert.equal(w.status, 'no_source');
      assert.equal('usedPct' in w, false);
      assert.equal('usedAbsolute' in w, false);
    }
  });
});

describe('mapActionsFromG1', () => {
  const now = Date.parse('2026-08-15T12:00:00.000Z');

  it('maps minutes + amount due from a live G1 painel', () => {
    const snap = mapActionsFromG1({
      fonte: 'billing',
      cota: { usados_min: 900, incluidos_min: 2000, pagos_min: 0, pct: 45 },
      custo: { usd: 12.34, limite_usd: 50, pct_limite: 25 },
    }, now);
    assert.equal(snap.source, 'actions');
    const [min, pay] = snap.windows;
    assert.equal(min.name, 'minutos');
    assert.equal(min.usedPct, 45);
    assert.equal(min.usedAbsolute, 900);
    assert.equal(min.unit, 'min');
    assert.equal(min.status, 'ok');
    assert.equal(min.resetAt, '2026-09-01T00:00:00.000Z');
    assert.equal(pay.name, 'a_pagar');
    assert.equal(pay.usedAbsolute, 12.34);
    assert.equal(pay.unit, 'usd');
    assert.equal(pay.status, 'ok');
  });

  it('omits usedPct when G1 did not send it — never invents 0', () => {
    const snap = mapActionsFromG1({
      cota: { usados_min: 300, incluidos_min: 2000, pagos_min: 0 },
      custo: { usd: 0 },
    }, now);
    const [min, pay] = snap.windows;
    assert.equal('usedPct' in min, false);
    assert.equal(min.usedAbsolute, 300);
    assert.equal(min.status, 'ok');
    assert.equal('usedPct' in pay, false);
    assert.equal(pay.usedAbsolute, 0); // measured zero dollars is real
    assert.equal(pay.status, 'ok');
  });

  it('empty / missing G1 → no_source on both windows', () => {
    const snap = mapActionsFromG1(null, now);
    assert.equal(snap.error, 'g1_empty');
    for (const w of snap.windows) {
      assert.equal(w.status, 'no_source');
      assert.equal('usedPct' in w, false);
    }
  });

  it('G1 cota without usados_min is no_source, not 0%', () => {
    const snap = mapActionsFromG1({ cota: { pct: 0 }, custo: {} }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
  });
});

describe('painelLiteFromBilling', () => {
  it('uses plan quota while nothing is paid (G1 2026-08-01 fix)', () => {
    const lite = painelLiteFromBilling(
      { usados_min: 300, pagos_min: 0, incluidos_min: 300, usd: 0 },
      { ghIncluidos: 2000 },
      Date.parse('2026-08-01T03:00:00.000Z'),
    );
    assert.equal(lite.cota.incluidos_min, 2000);
    assert.equal(lite.cota.pct, 15);
    assert.notEqual(lite.cota.pct, 100);
  });

  it('uses billing included minutes once something is paid', () => {
    const lite = painelLiteFromBilling(
      { usados_min: 2500, pagos_min: 260, incluidos_min: 2240, usd: 1.56 },
      { ghIncluidos: 2000 },
      Date.now(),
    );
    assert.equal(lite.cota.incluidos_min, 2240);
    assert.equal(lite.cota.pct, 112);
  });
});

describe('emptyPayload', () => {
  it('five sources, none invent 0', () => {
    const p = emptyPayload(Date.parse('2026-08-31T00:00:00.000Z'));
    assert.equal(p.sources.length, 5);
    assert.deepEqual(p.sources.map((s) => s.source),
      ['claude', 'codex', 'actions', 'gemini', 'cursor']);
    for (const s of p.sources) {
      for (const w of s.windows) assert.equal('usedPct' in w, false);
    }
  });
});

describe('nextCycleResetIso', () => {
  it('is 00:00 UTC on the 1st of next month', () => {
    assert.equal(nextCycleResetIso(Date.parse('2026-08-31T21:00:00.000Z')),
      '2026-09-01T00:00:00.000Z');
  });
});

describe('mapCodexFromCodexBar', () => {
  const now = Date.parse('2026-08-31T18:00:00.000Z');

  it('maps primary 5h + secondary 7d with reset', () => {
    const snap = mapCodexFromCodexBar({
      provider: 'codex',
      source: 'oauth',
      usage: {
        primary: { usedPercent: 28, windowMinutes: 300, resetsAt: '2026-08-31T19:15:00.000Z' },
        secondary: { usedPercent: 59, windowMinutes: 10080, resetsAt: '2026-09-05T17:00:00.000Z' },
      },
    }, now);
    assert.equal(snap.source, 'codex');
    assert.equal(snap.windows[0].name, '5h');
    assert.equal(snap.windows[0].usedPct, 28);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(snap.windows[0].resetAt, '2026-08-31T19:15:00.000Z');
    assert.equal(snap.windows[1].name, '7d');
    assert.equal(snap.windows[1].usedPct, 59);
    assert.equal(snap.windows[1].resetAt, '2026-09-05T17:00:00.000Z');
    assert.equal(snap.via, 'oauth');
  });

  it('measured 0% is kept (window unused)', () => {
    const snap = mapCodexFromCodexBar({
      usage: { primary: { usedPercent: 0, resetsAt: '2026-08-31T19:15:00.000Z' }, secondary: null },
    }, now);
    assert.equal(snap.windows[0].usedPct, 0);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(snap.windows[1].status, 'no_source');
    assert.equal('usedPct' in snap.windows[1], false);
  });

  it('omitted usedPercent / synthetic placeholder is no_source, never 0', () => {
    const snap = mapCodexFromCodexBar({
      usage: {
        primary: { windowMinutes: 300, resetsAt: '2026-08-31T19:15:00.000Z' },
        secondary: { usedPercent: 0, isSyntheticPlaceholder: true },
      },
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(snap.windows[1].status, 'no_source');
    assert.equal('usedPct' in snap.windows[1], false);
  });

  it('picks the Codex entry from a GET /usage array', () => {
    const snap = mapCodexFromCodexBar([
      { provider: 'claude', usage: { primary: { usedPercent: 9 } } },
      { provider: 'codex', usage: { primary: { usedPercent: 41 } } },
    ], now);
    assert.equal(snap.windows[0].usedPct, 41);
  });
});

describe('mapCodexFromWham', () => {
  const now = Date.parse('2026-08-31T12:00:00.000Z');

  it('maps primary_window / secondary_window + unix reset_at', () => {
    const snap = mapCodexFromWham({
      plan_type: 'plus',
      rate_limit: {
        primary_window: {
          used_percent: 55,
          limit_window_seconds: 18000,
          reset_after_seconds: 2547,
          reset_at: 1756653307,
        },
        secondary_window: {
          used_percent: 51,
          limit_window_seconds: 604800,
          reset_at: 1757157165,
        },
      },
    }, now);
    assert.equal(snap.windows[0].usedPct, 55);
    assert.equal(snap.windows[0].resetAt, new Date(1756653307 * 1000).toISOString());
    assert.equal(snap.windows[1].usedPct, 51);
    assert.equal(snap.windows[1].status, 'ok');
  });

  it('missing used_percent is no_source, not 0', () => {
    const snap = mapCodexFromWham({
      rate_limit: {
        primary_window: { reset_at: 1756653307 },
        secondary_window: { used_percent: 12 },
      },
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(snap.windows[1].usedPct, 12);
  });

  it('uses reset_after_seconds when reset_at is absent', () => {
    const snap = mapCodexFromWham({
      rate_limit: { primary_window: { used_percent: 10, reset_after_seconds: 120 } },
    }, now);
    assert.equal(snap.windows[0].resetAt, '2026-08-31T12:02:00.000Z');
  });

  it('empty body → no_source, never 0', () => {
    const snap = mapCodexFromWham(null, now);
    assert.equal(snap.error, 'wham_empty');
    assert.equal('usedPct' in snap.windows[0], false);
  });
});

describe('mapCodexFromAppServer', () => {
  const now = Date.parse('2026-08-31T12:00:00.000Z');

  it('maps rateLimits.primary usedPercent + unix resetsAt', () => {
    const snap = mapCodexFromAppServer({
      rateLimits: {
        limitId: 'codex',
        primary: { usedPercent: 25, windowDurationMins: 300, resetsAt: 1756653307 },
        secondary: { usedPercent: 18, windowDurationMins: 10080, resetsAt: 1757157165 },
      },
    }, now);
    assert.equal(snap.windows[0].usedPct, 25);
    assert.equal(snap.windows[0].resetAt, new Date(1756653307 * 1000).toISOString());
    assert.equal(snap.windows[1].usedPct, 18);
  });

  it('accepts a recoverable wham/usage body', () => {
    const snap = mapCodexFromAppServer({
      rate_limit: { primary_window: { used_percent: 33, reset_at: 1756653307 } },
    }, now);
    assert.equal(snap.windows[0].usedPct, 33);
  });
});

describe('recoverWhamFromText', () => {
  it('extracts a rate_limit object from an error string', () => {
    const obj = recoverWhamFromText('boom {"rate_limit":{"primary_window":{"used_percent":7}}} tail');
    assert.equal(obj.rate_limit.primary_window.used_percent, 7);
  });
});
