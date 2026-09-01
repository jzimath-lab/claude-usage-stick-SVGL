'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const {
  noSource, mapActionsFromG1, painelLiteFromBilling, statusFromPct,
  nextCycleResetIso, emptyPayload,
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
