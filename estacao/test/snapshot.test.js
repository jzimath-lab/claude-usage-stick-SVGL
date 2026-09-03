'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const {
  noSource, mapActionsFromG1, painelLiteFromBilling, statusFromPct,
  nextCycleResetIso, emptyPayload,
  mapCodexFromCodexBar, mapCodexFromWham, mapCodexFromAppServer,
  recoverWhamFromText,
  mapCursorFromCodexBar, mapCursorFromUsageSummary, grokWindowFromSand,
  planIncludedPct, hasSourcedUsage,
  mapGeminiFromCodexBar, mapGeminiFromQuota, isConsumerShutdown, usedPctFromRemaining,
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

describe('mapCursorFromCodexBar', () => {
  const now = Date.parse('2026-08-31T18:00:00.000Z');

  it('maps included vs on-demand + Grok extra window', () => {
    const snap = mapCursorFromCodexBar({
      provider: 'cursor',
      source: 'web',
      usage: {
        primary: { usedPercent: 41, resetsAt: '2026-09-15T00:00:00.000Z' },
        secondary: { usedPercent: 10 },
        providerCost: { used: 4.2, limit: 20, resetsAt: '2026-09-15T00:00:00.000Z' },
        extraRateWindows: [{
          id: 'cursor-grok-bot',
          title: 'Grok Bot',
          window: { usedPercent: 12, resetsAt: '2026-09-07T00:00:00.000Z' },
        }],
      },
    }, now);
    assert.equal(snap.source, 'cursor');
    assert.equal(snap.windows[0].name, 'incluido');
    assert.equal(snap.windows[0].usedPct, 41);
    assert.equal(snap.windows[0].resetAt, '2026-09-15T00:00:00.000Z');
    assert.equal(snap.windows[1].name, 'on_demand');
    assert.equal(snap.windows[1].usedPct, 21);
    assert.equal(snap.windows[1].usedAbsolute, 4.2);
    assert.equal(snap.windows[1].unit, 'usd');
    assert.equal(snap.windows[2].name, 'grok_bot');
    assert.equal(snap.windows[2].usedPct, 12);
    assert.equal(snap.windows.length, 3);
    assert.equal(snap.via, 'web');
  });

  it('does not treat CodexBar Auto/secondary as on-demand', () => {
    const snap = mapCursorFromCodexBar({
      provider: 'cursor',
      usage: { primary: { usedPercent: 8 }, secondary: { usedPercent: 99 } },
    }, now);
    assert.equal(snap.windows[0].usedPct, 8);
    assert.equal(snap.windows[1].status, 'no_source');
    assert.equal('usedPct' in snap.windows[1], false);
    assert.equal(snap.windows.length, 2);
  });

  it('omits Grok when extra window has no usedPercent', () => {
    const snap = mapCursorFromCodexBar({
      provider: 'cursor',
      usage: {
        primary: { usedPercent: 3 },
        providerCost: { used: 0, limit: 10 },
        extraRateWindows: [{ id: 'cursor-grok-bot', title: 'Grok Bot', window: { resetsAt: '2026-09-07T00:00:00.000Z' } }],
      },
    }, now);
    assert.equal(snap.windows.length, 2);
    assert.equal(snap.windows[1].usedPct, 0);
    assert.ok(!snap.windows.some((w) => w.name === 'grok_bot'));
  });

  it('ignores a Codex-only payload', () => {
    const snap = mapCursorFromCodexBar({
      provider: 'codex',
      usage: { primary: { usedPercent: 28 } },
    }, now);
    assert.equal(snap.error, 'codexbar_empty');
    assert.equal(snap.windows[0].status, 'no_source');
  });
});

describe('mapCursorFromUsageSummary', () => {
  const now = Date.parse('2026-08-31T12:00:00.000Z');

  it('maps plan percent + on-demand cents and keeps Grok when usagePercent exists', () => {
    const snap = mapCursorFromUsageSummary({
      billingCycleEnd: '2026-09-15T00:00:00.000Z',
      individualUsage: {
        plan: { used: 820, limit: 2000, totalPercentUsed: 41 },
        onDemand: { used: 420, limit: 2000 },
      },
    }, now, { usagePercent: 12, nextResetTimestampUtc: '2026-09-07T00:00:00.000Z' });
    assert.equal(snap.windows[0].usedPct, 41);
    assert.equal(snap.windows[0].resetAt, '2026-09-15T00:00:00.000Z');
    assert.equal(snap.windows[1].usedPct, 21);
    assert.equal(snap.windows[1].usedAbsolute, 4.2);
    assert.equal(snap.windows[2].usedPct, 12);
  });

  it('missing plan percent is no_source, never CodexBar fallback 0', () => {
    const snap = mapCursorFromUsageSummary({
      individualUsage: { plan: { enabled: true }, onDemand: { used: 0, limit: 1000 } },
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(snap.windows[1].usedPct, 0); // measured unused on-demand
    assert.equal(snap.windows.length, 2);
  });

  it('omitted Grok usagePercent does not add a 0% window', () => {
    const snap = mapCursorFromUsageSummary({
      individualUsage: { plan: { totalPercentUsed: 5 } },
    }, now, { hasNonZeroIncludedLimit: false });
    assert.equal(snap.windows[0].usedPct, 5);
    assert.equal(snap.windows.length, 2);
    assert.equal(grokWindowFromSand({ hasAvailableUsage: true }), null);
    assert.equal(grokWindowFromSand({ usagePercent: 0 }).usedPct, 0);
  });

  it('uses used/limit when totalPercentUsed is absent', () => {
    const snap = mapCursorFromUsageSummary({
      individualUsage: { plan: { used: 500, limit: 2000 } },
    }, now);
    assert.equal(snap.windows[0].usedPct, 25);
  });

  it('does not invent included from Auto+API mean (not 50%)', () => {
    const snap = mapCursorFromUsageSummary({
      individualUsage: {
        plan: { autoPercentUsed: 0, apiPercentUsed: 100 },
        onDemand: { used: 0, limit: 1000 },
      },
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.notEqual(snap.windows[0].usedPct, 50);
    assert.equal(snap.windows[1].usedPct, 0); // measured unused on-demand
    assert.equal(planIncludedPct({ autoPercentUsed: 0, apiPercentUsed: 100 }), undefined);
    assert.equal(planIncludedPct({ totalPercentUsed: 0 }), 0);
  });

  it('uses overall / pooled used/limit when plan has no total', () => {
    const viaOverall = mapCursorFromUsageSummary({
      individualUsage: { overall: { used: 10, limit: 40 } },
    }, now);
    assert.equal(viaOverall.windows[0].usedPct, 25);
    const viaPooled = mapCursorFromUsageSummary({
      teamUsage: { pooled: { used: 3, limit: 10 } },
    }, now);
    assert.equal(viaPooled.windows[0].usedPct, 30);
  });
});

describe('mapGeminiFromCodexBar', () => {
  const now = Date.parse('2026-08-31T18:00:00.000Z');

  it('maps primary hoje + secondary ciclo with reset', () => {
    const snap = mapGeminiFromCodexBar({
      provider: 'gemini',
      source: 'oauth',
      usage: {
        primary: { usedPercent: 42, windowMinutes: 1440, resetsAt: '2026-09-01T07:00:00.000Z' },
        secondary: { usedPercent: 15, windowMinutes: 1440, resetsAt: '2026-09-01T07:00:00.000Z' },
      },
    }, now);
    assert.equal(snap.source, 'gemini');
    assert.equal(snap.windows[0].name, 'hoje');
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(snap.windows[0].resetAt, '2026-09-01T07:00:00.000Z');
    assert.equal(snap.windows[1].name, 'ciclo');
    assert.equal(snap.windows[1].usedPct, 15);
    assert.equal(snap.via, 'oauth');
  });

  it('omitted usedPercent is no_source, never 0', () => {
    const snap = mapGeminiFromCodexBar({
      provider: 'gemini',
      usage: { primary: { resetsAt: '2026-09-01T07:00:00.000Z' }, secondary: {} },
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(snap.windows[1].status, 'no_source');
    assert.equal('usedPct' in snap.windows[1], false);
  });

  it('measured 0% stays 0', () => {
    const snap = mapGeminiFromCodexBar({
      provider: 'gemini',
      usage: { primary: { usedPercent: 0 }, secondary: { usedPercent: 0 } },
    }, now);
    assert.equal(snap.windows[0].usedPct, 0);
    assert.equal(snap.windows[0].status, 'ok');
    assert.equal(hasSourcedUsage(snap), true);
  });

  it('consumer shutdown is SEM FONTE, never 0, never Antigravity', () => {
    const snap = mapGeminiFromCodexBar({
      provider: 'gemini',
      error: 'UNSUPPORTED_CLIENT: please migrate to Antigravity for Gemini',
    }, now);
    assert.equal(snap.error, 'consumer_shutdown');
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(isConsumerShutdown('IneligibleTierError'), true);
    assert.equal(isConsumerShutdown({ error: { status: 'SUBSCRIPTION_REQUIRED' } }), true);
    assert.equal(isConsumerShutdown('plain 403'), false);
  });

  it('ignores a Codex-only payload', () => {
    const snap = mapGeminiFromCodexBar({
      provider: 'codex',
      usage: { primary: { usedPercent: 28 } },
    }, now);
    assert.equal(snap.error, 'codexbar_empty');
    assert.equal(snap.windows[0].status, 'no_source');
  });
});

describe('mapGeminiFromQuota', () => {
  const now = Date.parse('2026-08-31T18:00:00.000Z');

  it('maps Pro hoje + Flash ciclo from remainingFraction (CodexBar 100 − percentLeft)', () => {
    const snap = mapGeminiFromQuota({
      buckets: [
        { modelId: 'gemini-2.5-pro', remainingFraction: 0.58, resetTime: '2026-09-01T07:00:00.000Z' },
        { modelId: 'gemini-2.5-flash', remainingFraction: 0.85, resetTime: '2026-09-01T07:00:00.000Z' },
        { modelId: 'gemini-2.5-pro', remainingFraction: 0.9, tokenType: 'output' },
      ],
    }, now);
    assert.equal(usedPctFromRemaining(0.58), 42);
    assert.equal(snap.windows[0].name, 'hoje');
    assert.equal(snap.windows[0].usedPct, 42);
    assert.equal(snap.windows[0].resetAt, '2026-09-01T07:00:00.000Z');
    assert.equal(snap.windows[1].name, 'ciclo');
    assert.equal(snap.windows[1].usedPct, 15);
  });

  it('omitted remainingFraction never becomes 0%', () => {
    const snap = mapGeminiFromQuota({
      buckets: [
        { modelId: 'gemini-2.5-pro', resetTime: '2026-09-01T07:00:00.000Z' },
        { modelId: 'gemini-2.5-flash' },
      ],
    }, now);
    assert.equal(snap.windows[0].status, 'no_source');
    assert.equal('usedPct' in snap.windows[0], false);
    assert.equal(snap.error, 'quota_fraction_omitted');
  });

  it('measured remainingFraction 1.0 is 0% used; 0.0 is 100%', () => {
    const idle = mapGeminiFromQuota({
      buckets: [{ modelId: 'gemini-2.5-pro', remainingFraction: 1 }],
    }, now);
    assert.equal(idle.windows[0].usedPct, 0);
    assert.equal(idle.windows[0].status, 'ok');
    assert.equal(idle.windows[1].status, 'no_source');
    assert.equal('usedPct' in idle.windows[1], false);
    const full = mapGeminiFromQuota({
      buckets: [{ modelId: 'gemini-2.5-pro', remainingFraction: 0 }],
    }, now);
    assert.equal(full.windows[0].usedPct, 100);
    assert.equal(full.windows[0].status, 'blocked');
  });

  it('empty / shutdown body is no_source, never 0', () => {
    assert.equal(mapGeminiFromQuota(null, now).windows[0].status, 'no_source');
    const shut = mapGeminiFromQuota({
      error: { status: 'SUBSCRIPTION_REQUIRED', message: 'UNSUPPORTED_CLIENT' },
    }, now);
    assert.equal(shut.error, 'consumer_shutdown');
    assert.equal('usedPct' in shut.windows[0], false);
  });
});

describe('hasSourcedUsage', () => {
  it('treats measured 0% as sourced and omitted as not', () => {
    assert.equal(hasSourcedUsage({
      windows: [{ name: '5h', usedPct: 0, status: 'ok' }, { name: '7d', status: 'no_source' }],
    }), true);
    assert.equal(hasSourcedUsage({
      windows: [{ name: '5h', status: 'no_source' }, { name: '7d', status: 'no_source' }],
    }), false);
    assert.equal(hasSourcedUsage(noSource('codex', '2026-08-31T00:00:00.000Z')), false);
  });

  it('treats finite usedAbsolute as sourced without inventing 0%', () => {
    const usdOnly = {
      windows: [
        { name: 'incluido', status: 'no_source' },
        { name: 'on_demand', usedAbsolute: 4.2, unit: 'usd', status: 'ok' },
      ],
    };
    assert.equal(hasSourcedUsage(usdOnly), true);
    assert.equal('usedPct' in usdOnly.windows[1], false);
    assert.equal(hasSourcedUsage({
      windows: [
        { name: 'incluido', status: 'no_source' },
        { name: 'on_demand', usedAbsolute: 0, unit: 'usd', status: 'ok' },
      ],
    }), true);
    const now = Date.parse('2026-08-31T18:00:00.000Z');
    const snap = mapCursorFromCodexBar({
      provider: 'cursor',
      usage: { providerCost: { used: 4.2 } },
    }, now);
    assert.equal(snap.windows[1].usedAbsolute, 4.2);
    assert.equal('usedPct' in snap.windows[1], false);
    assert.equal(hasSourcedUsage(snap), true);
  });
});
