'use strict';

/** QuotaSnapshot contract (spec v1 / ZYN-568). usedPct omitted → no_source, never invent 0. */

const SOURCES = ['claude', 'codex', 'actions', 'gemini', 'cursor'];

const LABELS = {
  claude: 'Claude',
  codex: 'Codex',
  cursor: 'Cursor',
  actions: 'GitHub Actions',
  gemini: 'Gemini',
};

const STUB_WINDOWS = {
  claude: ['5h', '7d'],
  codex: ['5h', '7d'],
  cursor: ['incluido', 'on_demand'],
  actions: ['minutos', 'a_pagar'],
  gemini: ['hoje', 'ciclo'],
};

function iso(ms) {
  return new Date(ms).toISOString();
}

function noSourceWindow(name) {
  return { name, status: 'no_source' };
}

function noSource(source, asOf, error) {
  const snap = {
    source,
    label: LABELS[source] || source,
    windows: (STUB_WINDOWS[source] || ['window']).map(noSourceWindow),
    asOf,
  };
  if (error) snap.error = String(error);
  return snap;
}

/** Status from a real usedPct. 0 is a measured value, not a missing field. */
function statusFromPct(pct) {
  if (typeof pct !== 'number' || Number.isNaN(pct)) return 'no_source';
  if (pct >= 100) return 'blocked';
  if (pct >= 70) return 'warning';
  return 'ok';
}

/** 00:00 UTC on the 1st of next month — G1 cycle boundary (ciclo.js). */
function nextCycleResetIso(agoraMs) {
  const d = new Date(agoraMs);
  return new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth() + 1, 1)).toISOString();
}

function cycleStartIso(agoraMs) {
  const d = new Date(agoraMs);
  const y = d.getUTCFullYear();
  const m = String(d.getUTCMonth() + 1).padStart(2, '0');
  return `${y}-${m}-01`;
}

/**
 * Map G1 /api/github painel JSON (github-painel.montar) → Actions QuotaSnapshot.
 * Minutes this month + amount due. Missing usedPct stays omitted (never 0).
 */
function mapActionsFromG1(painel, asOfMs) {
  const asOf = iso(asOfMs);
  if (!painel || typeof painel !== 'object') {
    return noSource('actions', asOf, 'g1_empty');
  }

  const cota = painel.cota;
  const custo = painel.custo;
  const minutos = { name: 'minutos', status: 'no_source' };
  const aPagar = { name: 'a_pagar', status: 'no_source' };

  if (cota && typeof cota === 'object') {
    if (typeof cota.usados_min === 'number' && !Number.isNaN(cota.usados_min)) {
      minutos.usedAbsolute = cota.usados_min;
      minutos.unit = 'min';
      minutos.resetAt = nextCycleResetIso(asOfMs);
      if (typeof cota.pct === 'number' && !Number.isNaN(cota.pct)) {
        minutos.usedPct = cota.pct;
        minutos.status = statusFromPct(cota.pct);
      } else {
        minutos.status = 'ok';
      }
    }
  }

  if (custo && typeof custo === 'object'
      && typeof custo.usd === 'number' && !Number.isNaN(custo.usd)) {
    aPagar.usedAbsolute = custo.usd;
    aPagar.unit = 'usd';
    if (typeof custo.pct_limite === 'number' && !Number.isNaN(custo.pct_limite)) {
      aPagar.usedPct = custo.pct_limite;
      aPagar.status = statusFromPct(custo.pct_limite);
    } else {
      aPagar.status = 'ok';
    }
  }

  const snap = {
    source: 'actions',
    label: LABELS.actions,
    windows: [minutos, aPagar],
    asOf,
  };
  if (painel.fonte) snap.g1Fonte = painel.fonte;
  return snap;
}

/**
 * Billing-only shape from G1 github.js buscarBilling, plus the painel
 * denominator rule (don't use included=usados while nothing is paid —
 * that painted 100% on a fresh cycle). Does not invent a price.
 */
function painelLiteFromBilling(billing, cfg, agoraMs) {
  if (!billing || typeof billing.usados_min !== 'number') return null;
  const planIncluidos = (cfg && cfg.ghIncluidos) || 2000;
  const incluidos = billing.pagos_min > 0 ? billing.incluidos_min : planIncluidos;
  return {
    fonte: 'billing',
    ciclo: { inicio: cycleStartIso(agoraMs) },
    cota: {
      usados_min: billing.usados_min,
      incluidos_min: incluidos,
      pagos_min: billing.pagos_min,
      ...(incluidos ? { pct: Math.round(billing.usados_min / incluidos * 100) } : {}),
    },
    custo: { usd: billing.usd },
  };
}

function emptyPayload(asOfMs, errors) {
  const asOf = iso(asOfMs);
  return {
    asOf,
    sources: SOURCES.map((id) => noSource(id, asOf, errors && errors[id])),
  };
}

module.exports = {
  SOURCES,
  LABELS,
  STUB_WINDOWS,
  iso,
  noSource,
  noSourceWindow,
  statusFromPct,
  nextCycleResetIso,
  cycleStartIso,
  mapActionsFromG1,
  painelLiteFromBilling,
  emptyPayload,
};
