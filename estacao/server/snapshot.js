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

function pickBarEntry(payload, provider) {
  if (!payload) return null;
  if (Array.isArray(payload)) {
    const named = payload.find((p) => p && p.provider === provider);
    if (named) return named;
    if (provider === 'codex') return payload.find((p) => p && p.usage) || null;
    return null;
  }
  if (typeof payload !== 'object') return null;
  if (Array.isArray(payload.providers)) return pickBarEntry(payload.providers, provider);
  if (payload[provider] && typeof payload[provider] === 'object' && payload[provider] !== payload) {
    return pickBarEntry(payload[provider], provider);
  }
  if (payload.provider && payload.provider !== provider) return null;
  if (payload.provider === provider || payload.usage || payload.primary
      || (provider === 'codex' && payload.rate_limit)) {
    return payload;
  }
  return null;
}

function pickCodexBarEntry(payload) {
  return pickBarEntry(payload, 'codex');
}

function pickCursorBarEntry(payload) {
  return pickBarEntry(payload, 'cursor');
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

/** On-demand % from CodexBar providerCost (used/limit). Missing limit → USD only, never invent 0%. */
function windowFromProviderCost(name, cost, resetFallback) {
  if (!cost || typeof cost !== 'object') return noSourceWindow(name);
  const used = firstNumber(cost.used, cost.usedUSD, cost.used_usd);
  const limit = firstNumber(cost.limit, cost.limitUSD, cost.limit_usd);
  const reset = resetAtIso(cost.resetsAt ?? cost.resetAt ?? cost.resets_at) || resetFallback;
  if (typeof used !== 'number') return noSourceWindow(name);
  const w = { name, usedAbsolute: used, unit: 'usd', status: 'ok' };
  if (reset) w.resetAt = reset;
  const pct = pctFromUsedLimit(used, limit);
  if (pct != null) {
    w.usedPct = pct;
    w.status = statusFromPct(pct);
  }
  return w;
}

function grokWindowFromExtra(extras) {
  if (!Array.isArray(extras)) return null;
  const hit = extras.find((e) => e && (
    e.id === 'cursor-grok-bot' || e.id === 'grok_bot' || e.title === 'Grok Bot'
  ));
  if (!hit) return null;
  const win = hit.window && typeof hit.window === 'object' ? hit.window : hit;
  if (win.usageKnown === false || win.isSyntheticPlaceholder === true) return null;
  const pct = firstNumber(win.usedPercent, win.used_percent, win.usagePercent);
  if (pct == null) return null;
  return windowFromPct('grok_bot', pct, resetAtIso(win.resetsAt ?? win.resetAt ?? win.nextResetTimestampUtc));
}

/**
 * Grok Bot weekly. Only when usagePercent is a real number (0 is unused week).
 * Omitted field → omit the window. Never invent 0%.
 */
function grokWindowFromSand(sand) {
  if (!sand || typeof sand !== 'object') return null;
  const pct = firstNumber(sand.usagePercent, sand.usage_percent, sand.usedPercent);
  if (pct == null) return null;
  return windowFromPct(
    'grok_bot',
    pct,
    resetAtIso(sand.nextResetTimestampUtc ?? sand.resetsAt ?? sand.resetAt),
  );
}

/**
 * `codexbar usage --format json --provider cursor` / GET /usage → QuotaSnapshot.
 * primary = included plan; providerCost = on-demand. Grok only from extraRateWindows
 * when usedPercent/usagePercent is present. Secondary (Auto) is not on-demand.
 */
function mapCursorFromCodexBar(payload, asOfMs) {
  const asOf = iso(asOfMs);
  const entry = pickCursorBarEntry(payload);
  if (!entry) return noSource('cursor', asOf, 'codexbar_empty');
  if (entry.error && !entry.usage && !entry.primary && !entry.providerCost) {
    const msg = typeof entry.error === 'string'
      ? entry.error
      : (entry.error.message || 'codexbar_error');
    return noSource('cursor', asOf, msg);
  }
  const usage = entry.usage || entry;
  const incluido = windowFromCodexBar('incluido', usage.primary);
  const onDemand = windowFromProviderCost(
    'on_demand',
    usage.providerCost || usage.provider_cost || usage.onDemand || usage.on_demand,
    resetAtIso(usage.primary && (usage.primary.resetsAt ?? usage.primary.resetAt)),
  );
  const windows = [incluido, onDemand];
  const grok = grokWindowFromExtra(usage.extraRateWindows || usage.extra_rate_windows || entry.extraRateWindows);
  if (grok) windows.push(grok);
  const snap = {
    source: 'cursor',
    label: LABELS.cursor,
    windows,
    asOf,
  };
  if (entry.source) snap.via = String(entry.source);
  return snap;
}

function centsToUsd(cents) {
  if (typeof cents !== 'number' || Number.isNaN(cents)) return undefined;
  return cents / 100;
}

function pctFromUsedLimit(used, limit) {
  if (typeof used !== 'number' || typeof limit !== 'number' || Number.isNaN(used) || Number.isNaN(limit)) {
    return undefined;
  }
  if (limit <= 0) return undefined;
  return Math.round((used / limit) * 100 * 1e4) / 1e4;
}

/**
 * Included-plan % only from totalPercentUsed or used/limit
 * (plan / overall / pooled). Never an arithmetic mean of Auto + API.
 */
function planIncludedPct(plan, overall, pooled) {
  if (!plan && !overall && !pooled) return undefined;
  if (plan && typeof plan === 'object') {
    const direct = firstNumber(plan.totalPercentUsed, plan.total_percent_used);
    if (direct != null) return direct;
    const fromPlan = pctFromUsedLimit(firstNumber(plan.used), firstNumber(plan.limit));
    if (fromPlan != null) return fromPlan;
  }
  if (overall && typeof overall === 'object') {
    const fromOverall = pctFromUsedLimit(firstNumber(overall.used), firstNumber(overall.limit));
    if (fromOverall != null) return fromOverall;
  }
  if (pooled && typeof pooled === 'object') {
    return pctFromUsedLimit(firstNumber(pooled.used), firstNumber(pooled.limit));
  }
  return undefined;
}

function onDemandWindowFromBlock(block, resetIso) {
  if (!block || typeof block !== 'object') return noSourceWindow('on_demand');
  const usedCents = firstNumber(block.used);
  const limitCents = firstNumber(block.limit);
  if (usedCents == null && limitCents == null) return noSourceWindow('on_demand');
  const usedUsd = usedCents == null ? undefined : centsToUsd(usedCents);
  const w = { name: 'on_demand', status: 'ok' };
  if (usedUsd != null) {
    w.usedAbsolute = usedUsd;
    w.unit = 'usd';
  }
  const pct = pctFromUsedLimit(usedCents, limitCents);
  if (pct != null) {
    w.usedPct = pct;
    w.status = statusFromPct(pct);
  } else if (usedUsd == null) {
    return noSourceWindow('on_demand');
  }
  if (resetIso) w.resetAt = resetIso;
  return w;
}

/**
 * GET cursor.com/api/usage-summary → included vs on-demand.
 * Missing percent fields stay omitted (never CodexBar's fallback 0).
 */
function mapCursorFromUsageSummary(body, asOfMs, sand) {
  const asOf = iso(asOfMs);
  if (!body || typeof body !== 'object') return noSource('cursor', asOf, 'usage_summary_empty');
  const individual = body.individualUsage || body.individual_usage || {};
  const team = body.teamUsage || body.team_usage || {};
  const plan = individual.plan;
  const reset = resetAtIso(body.billingCycleEnd || body.billing_cycle_end);
  const inclPct = planIncludedPct(plan, individual.overall, team.pooled);
  const incluido = windowFromPct('incluido', inclPct, reset);
  const onDemand = onDemandWindowFromBlock(
    (individual.onDemand || individual.on_demand || team.onDemand || team.on_demand),
    reset,
  );
  const windows = [incluido, onDemand];
  const grok = grokWindowFromSand(sand);
  if (grok) windows.push(grok);
  const snap = {
    source: 'cursor',
    label: LABELS.cursor,
    windows,
    asOf,
  };
  if (windows[0].status === 'no_source' && windows[1].status === 'no_source') {
    snap.error = 'usage_summary_windows_empty';
  }
  return snap;
}

/**
 * Preferred-collector snapshot is usable when some window has sourced usage
 * (finite usedPct or usedAbsolute, including measured 0). Both windows
 * no_source → keep walking the chain. Omitted fields are not 0.
 */
function hasSourcedUsage(snap) {
  if (!snap || !Array.isArray(snap.windows)) return false;
  return snap.windows.some((w) => {
    if (!w) return false;
    if (typeof w.usedPct === 'number' && !Number.isNaN(w.usedPct)) return true;
    if (typeof w.usedAbsolute === 'number' && !Number.isNaN(w.usedAbsolute)) return true;
    return false;
  });
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
  pickCursorBarEntry,
  mapCodexFromCodexBar,
  mapCodexFromWham,
  mapCodexFromAppServer,
  recoverWhamFromText,
  mapCursorFromCodexBar,
  mapCursorFromUsageSummary,
  grokWindowFromSand,
  planIncludedPct,
  hasSourcedUsage,
};
