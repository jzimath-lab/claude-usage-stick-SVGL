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

/** First finite number; missing / NaN stay undefined (never invent 0). */
function firstNumber(...vals) {
  for (const v of vals) {
    if (typeof v === 'number' && !Number.isNaN(v)) return v;
  }
  return undefined;
}

/**
 * ISO reset from CodexBar (`resetsAt` string), wham (`reset_at` unix s),
 * or app-server (`resetsAt` unix s). Relative `reset_after_seconds` needs nowMs.
 */
function resetAtIso(value, nowMs, afterSeconds) {
  if (typeof value === 'string' && value) {
    const t = Date.parse(value);
    if (!Number.isNaN(t)) return new Date(t).toISOString();
  }
  if (typeof value === 'number' && !Number.isNaN(value)) {
    if (value > 1e12) return new Date(value).toISOString();
    if (value > 1e9) return new Date(value * 1000).toISOString();
  }
  if (typeof afterSeconds === 'number' && !Number.isNaN(afterSeconds) && nowMs != null) {
    return new Date(nowMs + afterSeconds * 1000).toISOString();
  }
  return undefined;
}

function windowFromPct(name, pct, resetIso) {
  if (typeof pct !== 'number' || Number.isNaN(pct)) return noSourceWindow(name);
  const w = { name, usedPct: pct, status: statusFromPct(pct) };
  if (resetIso) w.resetAt = resetIso;
  return w;
}

function pickCodexBarEntry(payload) {
  if (!payload) return null;
  if (Array.isArray(payload)) {
    return payload.find((p) => p && p.provider === 'codex')
      || payload.find((p) => p && p.usage) || null;
  }
  if (typeof payload !== 'object') return null;
  if (Array.isArray(payload.providers)) return pickCodexBarEntry(payload.providers);
  if (payload.codex && typeof payload.codex === 'object' && payload.codex !== payload) {
    return pickCodexBarEntry(payload.codex);
  }
  if (payload.provider === 'codex' || payload.usage || payload.primary || payload.rate_limit) {
    return payload;
  }
  return null;
}

function windowFromCodexBar(name, w) {
  if (!w || typeof w !== 'object') return noSourceWindow(name);
  // CodexBar lesson: a synthesized 0% lane is "usage unknown", not idle.
  if (w.isSyntheticPlaceholder === true) return noSourceWindow(name);
  if (w.usageKnown === false) return noSourceWindow(name);
  const pct = firstNumber(w.usedPercent, w.used_percent);
  if (pct == null) return noSourceWindow(name);
  return windowFromPct(name, pct, resetAtIso(w.resetsAt ?? w.resetAt ?? w.resets_at));
}

/**
 * `codexbar usage --format json --provider codex` / `GET /usage` → QuotaSnapshot.
 * primary = 5h, secondary = 7d. Missing usedPercent stays omitted.
 */
function mapCodexFromCodexBar(payload, asOfMs) {
  const asOf = iso(asOfMs);
  const entry = pickCodexBarEntry(payload);
  if (!entry) return noSource('codex', asOf, 'codexbar_empty');
  if (entry.error && !entry.usage && !entry.primary) {
    const msg = typeof entry.error === 'string'
      ? entry.error
      : (entry.error.message || 'codexbar_error');
    return noSource('codex', asOf, msg);
  }
  const usage = entry.usage || entry;
  if (usage.rate_limit || usage.rateLimit) return mapCodexFromWham(usage, asOfMs);
  const snap = {
    source: 'codex',
    label: LABELS.codex,
    windows: [
      windowFromCodexBar('5h', usage.primary),
      windowFromCodexBar('7d', usage.secondary),
    ],
    asOf,
  };
  if (entry.source) snap.via = String(entry.source);
  return snap;
}

function windowFromWham(name, w, asOfMs) {
  if (!w || typeof w !== 'object') return noSourceWindow(name);
  const pct = firstNumber(w.used_percent, w.usedPercent);
  if (pct == null) return noSourceWindow(name);
  const reset = resetAtIso(
    w.reset_at ?? w.resetAt ?? w.resets_at ?? w.resetsAt,
    asOfMs,
    w.reset_after_seconds ?? w.resetAfterSeconds,
  );
  return windowFromPct(name, pct, reset);
}

/**
 * GET chatgpt.com/backend-api/wham/usage → QuotaSnapshot.
 * used_percent missing → no_source on that window (never 0).
 */
function mapCodexFromWham(body, asOfMs) {
  const asOf = iso(asOfMs);
  if (!body || typeof body !== 'object') return noSource('codex', asOf, 'wham_empty');
  const rl = body.rate_limit || body.rateLimit;
  if (!rl || typeof rl !== 'object') return noSource('codex', asOf, 'wham_no_rate_limit');
  const snap = {
    source: 'codex',
    label: LABELS.codex,
    windows: [
      windowFromWham('5h', rl.primary_window || rl.primaryWindow, asOfMs),
      windowFromWham('7d', rl.secondary_window || rl.secondaryWindow, asOfMs),
    ],
    asOf,
  };
  if (snap.windows.every((w) => w.status === 'no_source')) snap.error = 'wham_windows_empty';
  return snap;
}

function windowFromAppServer(name, w, asOfMs) {
  if (!w || typeof w !== 'object') return noSourceWindow(name);
  const pct = firstNumber(w.usedPercent, w.used_percent);
  if (pct == null) return noSourceWindow(name);
  return windowFromPct(name, pct, resetAtIso(w.resetsAt ?? w.resets_at ?? w.resetAt, asOfMs));
}

/**
 * `codex app-server` JSON-RPC `account/rateLimits/read` result → QuotaSnapshot.
 * Also accepts a recoverable wham/usage body nested in the result or error text.
 */
function mapCodexFromAppServer(result, asOfMs) {
  const asOf = iso(asOfMs);
  if (!result || typeof result !== 'object') return noSource('codex', asOf, 'app_server_empty');
  if (result.rate_limit || result.rateLimit) return mapCodexFromWham(result, asOfMs);
  const rl = result.rateLimits || result.rate_limits;
  if (!rl || typeof rl !== 'object') return noSource('codex', asOf, 'app_server_no_rate_limits');
  return {
    source: 'codex',
    label: LABELS.codex,
    windows: [
      windowFromAppServer('5h', rl.primary, asOfMs),
      windowFromAppServer('7d', rl.secondary, asOfMs),
    ],
    asOf,
  };
}

/** Pull a wham/usage JSON object out of an app-server error string, if present. */
function recoverWhamFromText(text) {
  if (text == null) return null;
  const s = typeof text === 'string' ? text : (() => {
    try { return JSON.stringify(text); } catch { return ''; }
  })();
  const i = s.indexOf('{');
  const j = s.lastIndexOf('}');
  if (i < 0 || j <= i) return null;
  try {
    const obj = JSON.parse(s.slice(i, j + 1));
    if (obj && (obj.rate_limit || obj.rateLimit)) return obj;
  } catch { /* not JSON */ }
  return null;
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
  firstNumber,
  resetAtIso,
  pickCodexBarEntry,
  mapCodexFromCodexBar,
  mapCodexFromWham,
  mapCodexFromAppServer,
  recoverWhamFromText,
};
