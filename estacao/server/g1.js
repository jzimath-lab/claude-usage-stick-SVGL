'use strict';

/**
 * Wrap G1 — do not invent a second GitHub client.
 *
 * Resolution order:
 *   1. G1_URL           — GET the live /api/github from estacao-server
 *   2. G1_FIXTURE       — tests only, a saved painel JSON
 *   3. G1_DIR + github.js buscarBilling — require the G1 module in-tree
 *
 * github.js talks to api.github.com. This file never does.
 */

const fs = require('fs');
const path = require('path');
const { mapActionsFromG1, painelLiteFromBilling, noSource, iso } = require('./snapshot');

const CANDIDATE_DIRS = [
  process.env.G1_DIR,
  '/docker/estacao',
  path.resolve(__dirname, '../../../Totvs-Moda-CRM/estacao'),
  path.resolve(__dirname, '../../Totvs-Moda-CRM/estacao'),
].filter(Boolean);

function resolveGithubJs(env = process.env) {
  if (env.G1_GITHUB_JS && fs.existsSync(env.G1_GITHUB_JS)) return env.G1_GITHUB_JS;
  const dirs = [env.G1_DIR, ...CANDIDATE_DIRS].filter(Boolean);
  for (const dir of dirs) {
    const p = path.join(dir, 'server/services/github.js');
    if (fs.existsSync(p)) return p;
  }
  return null;
}

function loadGithubModule(env = process.env, requireFn = require) {
  const file = resolveGithubJs(env);
  if (!file) return null;
  return requireFn(file);
}

async function fetchG1Url(url, token, fetchImpl = fetch) {
  const headers = { Accept: 'application/json' };
  if (token) {
    headers['X-Device-Token'] = token;
    headers.Authorization = `Bearer ${token}`;
  }
  const r = await fetchImpl(url, { headers, signal: AbortSignal.timeout(15000) });
  if (!r.ok) throw new Error(`g1_http_${r.status}`);
  return r.json();
}

function readFixture(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

async function collectActions({ now = Date.now(), env = process.env, fetchImpl = fetch, requireFn = require } = {}) {
  const asOf = iso(now);
  try {
    if (env.G1_URL) {
      const painel = await fetchG1Url(env.G1_URL, env.G1_DEVICE_TOKEN, fetchImpl);
      return mapActionsFromG1(painel, now);
    }
    if (env.G1_FIXTURE) {
      return mapActionsFromG1(readFixture(env.G1_FIXTURE), now);
    }
    const gh = loadGithubModule(env, requireFn);
    if (gh && typeof gh.buscarBilling === 'function') {
      if (!env.GITHUB_TOKEN || !env.GITHUB_USER) {
        return noSource('actions', asOf, 'g1_module_no_creds');
      }
      const billing = await gh.buscarBilling(
        { githubToken: env.GITHUB_TOKEN, githubUser: env.GITHUB_USER },
        now,
      );
      const painel = painelLiteFromBilling(billing, {
        ghIncluidos: Number(env.GH_INCLUIDOS) || 2000,
      }, now);
      return mapActionsFromG1(painel, now);
    }
    return noSource('actions', asOf, 'g1_unavailable');
  } catch (e) {
    return noSource('actions', asOf, e.message || 'g1_error');
  }
}

module.exports = { resolveGithubJs, loadGithubModule, collectActions, fetchG1Url };
