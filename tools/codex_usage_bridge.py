#!/usr/bin/env python3
"""
codex_usage_bridge.py — serve o consumo do Codex/ChatGPT para o Codex Usage Stick.

Roda no HOST da VPS do Hermes (não dentro do container). Lê o access_token que o
Hermes já mantém renovado em auth.json, chama GET /backend-api/codex/usage com os
headers do Codex CLI (sem eles = 403 Cloudflare), normaliza o rate_limit e serve
um JSON enxuto que o ESP32 busca.

Por que assim:
- Zero cota: é um GET de usage, não uma chamada de chat.
- Sem token no device e sem refresh de JWT: o Hermes renova o token 24h; o bridge
  só relê o arquivo a cada fetch.
- Não toca no Hermes: lê o auth.json pelo path do volume Docker, read-only.

Config por env:
  CODEX_BRIDGE_TOKEN   segredo que o device manda em Authorization: Bearer (obrigatório p/ servir)
  CODEX_BRIDGE_PORT    porta (default 8car... 8477)
  CODEX_AUTH_PATH      path do auth.json (default = volume do Hermes)
  CODEX_CACHE_SEC      idade máxima do cache antes de refetch (default 60)

Sem dependências externas (stdlib).
"""
import base64
import datetime
import json
import os
import time
import urllib.request
import urllib.error
from collections import defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

AUTH_PATH = os.environ.get(
    "CODEX_AUTH_PATH",
    "/var/lib/docker/volumes/hermes-webui-moe3_hermes-home/_data/auth.json",
)
USAGE_URL = "https://chatgpt.com/backend-api/codex/usage"
# Analytics (mesma fonte que a página chatgpt.com/codex/cloud/settings/analytics):
# 1 endpoint entrega tudo — turns(interações), credits, por cliente(origem) e por modelo, por dia.
ANALYTICS_BASE = "https://chatgpt.com/backend-api/wham/analytics/daily-workspace-usage-counts"
CACHE_SEC = int(os.environ.get("CODEX_CACHE_SEC", "60"))
# Analytics é dado diário (muda devagar) e mais pesado (30 dias) → cache mais longo.
AN_CACHE_SEC = int(os.environ.get("CODEX_AN_CACHE_SEC", "300"))
AN_RANGE_DAYS = int(os.environ.get("CODEX_AN_RANGE_DAYS", "30"))
AN_DAILY_KEEP = int(os.environ.get("CODEX_AN_DAILY_KEEP", "14"))  # dias no mini-histórico
AN_TOPN = int(os.environ.get("CODEX_AN_TOPN", "5"))               # top-N origens/modelos
PORT = int(os.environ.get("CODEX_BRIDGE_PORT", "8477"))
SHARED_TOKEN = os.environ.get("CODEX_BRIDGE_TOKEN", "")

# Headers que fazem o Cloudflare deixar passar — validados 24/07/2026.
# Sem originator/User-Agent codex_cli_rs a resposta é 403 HTML.
CODEX_UA = "codex_cli_rs/0.20.0"


def _read_auth():
    """access_token (renovado pelo Hermes) + account_id (claim do JWT)."""
    d = json.load(open(AUTH_PATH))
    tok = d["providers"]["openai-codex"]["tokens"]["access_token"]
    p = tok.split(".")[1]
    p += "=" * (-len(p) % 4)
    claims = json.loads(base64.urlsafe_b64decode(p))
    acc = claims.get("https://api.openai.com/auth", {}).get("chatgpt_account_id", "")
    return tok, acc


def _get_json(url, timeout=15):
    """GET autenticado com os headers que passam o Cloudflare. Devolve JSON."""
    tok, acc = _read_auth()
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {tok}",
        "chatgpt-account-id": acc,
        "OpenAI-Beta": "responses=experimental",
        "originator": "codex_cli_rs",
        "User-Agent": CODEX_UA,
    })
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def _fetch_raw():
    return _get_json(USAGE_URL)


def _fetch_analytics():
    end = datetime.date.today()
    start = end - datetime.timedelta(days=AN_RANGE_DAYS - 1)
    url = (f"{ANALYTICS_BASE}?start_date={start.isoformat()}"
           f"&end_date={end.isoformat()}&group_by=day&workspace_user=true")
    return _get_json(url, timeout=20)


def _short_surface(client_id):
    """CODEX_CLI -> cli, CODEX_GITHUB -> github, CODEX_WORK_WEB -> web, etc."""
    s = (client_id or "").upper().replace("CODEX_", "").lower()
    return {
        "cli": "cli",
        "github": "github",
        "work_web": "web",
        "web": "web",
        "desktop_app": "desktop",
        "work_desktop": "desktop_work",
    }.get(s, s or "unknown")


def _short_model(model):
    """gpt-5.6-luna -> luna, gpt-5.5 -> 5.5, codex-auto-review -> auto-review."""
    m = model or "?"
    if m.startswith("gpt-"):
        rest = m[4:]
        return rest.split("-", 1)[1] if "-" in rest else rest
    return m.replace("codex-", "")


def _rank(totby, keyname, valname, total):
    items = sorted(totby.items(), key=lambda kv: -kv[1])
    out = []
    for k, v in items[:AN_TOPN]:
        if v <= 0:
            continue
        out.append({
            keyname: k,
            valname: round(v, 1),
            "pct": round(100.0 * v / total, 1) if total else 0.0,
        })
    return out


def _norm_window(w):
    if not w:
        return None
    return {
        "used_percent": round(float(w.get("used_percent", 0)), 1),
        "window_seconds": int(w.get("limit_window_seconds", 0)),
        "reset_after": int(w.get("reset_after_seconds", 0)),
        "reset_at": int(w.get("reset_at", 0)),
    }


def normalize(raw):
    """
    Mapeia as janelas por DURAÇÃO, não pelo rótulo primary/secondary — o rótulo
    varia (às vezes primary=7d, às vezes 5h). 5h = 18000s, semana = 604800s.
    Assim o device sempre sabe qual card é qual.
    """
    rl = raw.get("rate_limit", raw) or {}
    windows = [rl.get("primary_window"), rl.get("secondary_window")]
    h5 = d7 = None
    for w in windows:
        if not w:
            continue
        secs = int(w.get("limit_window_seconds", 0))
        if secs <= 6 * 3600:      # 5h (18000) e afins
            h5 = _norm_window(w)
        else:                     # semana (604800)
            d7 = _norm_window(w)
    return {
        "ok": True,
        "plan": raw.get("plan_type") or rl.get("plan_type") or "",
        "allowed": bool(rl.get("allowed", True)),
        "limit_reached": bool(rl.get("limit_reached", False)),
        "h5": h5,
        "d7": d7,
        "ts": int(time.time()),
    }


def normalize_analytics(raw):
    """
    A partir de daily-workspace-usage-counts (30 dias), agrega o que as telas
    novas precisam:
      - interactions: total de turns (o número "Interações" da página)
      - credits_total: créditos consumidos no período
      - by_surface:  origem do consumo (imagem 1) — créditos por cliente, top-N + %
      - by_model:    modelo consumido (imagem 2) — turns por modelo, top-N + %
      - surf_order:  ordem canônica das origens (p/ cores consistentes no device)
      - daily:       últimos N dias {d, credits, turns, v:[créditos por origem
                     na ordem de surf_order]} p/ o gráfico diário EMPILHADO por origem
    Tudo derivado de UM endpoint (mesma auth, GET, zero cota).
    """
    days = raw.get("data", []) or []
    cred_by_surface = defaultdict(float)
    turns_by_model = defaultdict(float)
    tot_turns = tot_threads = 0.0
    tot_credits = 0.0
    daily = []                       # inclui surf:{origem:créditos} p/ empilhar depois
    for day in days:
        t = day.get("totals", {}) or {}
        tot_turns += t.get("turns", 0) or 0
        tot_threads += t.get("threads", 0) or 0
        tot_credits += t.get("credits", 0) or 0
        dsurf = defaultdict(float)
        for cl in day.get("clients", []) or []:
            k = _short_surface(cl.get("client_id"))
            c = cl.get("credits", 0) or 0
            cred_by_surface[k] += c
            dsurf[k] += c
        for m in day.get("models", []) or []:
            turns_by_model[_short_model(m.get("model"))] += m.get("turns", 0) or 0
        d = day.get("date", "")
        daily.append({
            "d": d[5:] if len(d) >= 10 else d,  # "MM-DD"
            "credits": int(round(t.get("credits", 0) or 0)),
            "turns": int(t.get("turns", 0) or 0),
            "surf": dsurf,
        })
    by_surface = _rank(cred_by_surface, "src", "credits", tot_credits)
    surf_order = [it["src"] for it in by_surface]        # top-N, ordem estável
    daily_out = [{
        "d": dd["d"], "credits": dd["credits"], "turns": dd["turns"],
        "v": [int(round(dd["surf"].get(k, 0))) for k in surf_order],
    } for dd in daily[-AN_DAILY_KEEP:]]
    return {
        "range_days": AN_RANGE_DAYS,
        "interactions": int(tot_turns),
        "threads": int(tot_threads),
        "credits_total": round(tot_credits, 1),
        "by_surface": by_surface,
        "by_model": _rank(turns_by_model, "model", "turns", tot_turns),
        "surf_order": surf_order,
        "daily": daily_out,
    }


class _Cache:
    data = None
    at = 0.0
    err = None


class _AnCache:
    data = None
    at = 0.0
    err = None


def get_analytics(force=False):
    """Cache independente do /usage — analytics muda devagar (dado diário)."""
    now = time.time()
    if not force and _AnCache.data and (now - _AnCache.at) < AN_CACHE_SEC:
        return _AnCache.data
    try:
        norm = normalize_analytics(_fetch_analytics())
        _AnCache.data, _AnCache.at, _AnCache.err = norm, now, None
        return norm
    except urllib.error.HTTPError as e:
        _AnCache.err = f"http_{e.code}"
    except Exception as e:
        _AnCache.err = type(e).__name__
    # falha nas analytics NÃO derruba o /usage — devolve último bom (stale) ou erro
    if _AnCache.data:
        stale = dict(_AnCache.data)
        stale["stale_age"] = int(now - _AnCache.at) if _AnCache.at else None
        stale["error"] = _AnCache.err
        return stale
    return {"error": _AnCache.err}


def get_usage(force=False):
    now = time.time()
    if not force and _Cache.data and (now - _Cache.at) < CACHE_SEC:
        return _Cache.data
    try:
        norm = normalize(_fetch_raw())
        _Cache.data, _Cache.at, _Cache.err = norm, now, None
        return norm
    except urllib.error.HTTPError as e:
        _Cache.err = f"http_{e.code}"
    except Exception as e:  # rede, parse, auth.json ausente
        _Cache.err = type(e).__name__
    # falha: devolve o último bom marcado como stale, ou um erro explícito
    stale = dict(_Cache.data) if _Cache.data else {"ok": False, "h5": None, "d7": None}
    stale["ok"] = False
    stale["error"] = _Cache.err
    stale["stale_age"] = int(now - _Cache.at) if _Cache.at else None
    return stale


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") == "/health":
            return self._send(200, {"ok": True})
        if self.path.rstrip("/") != "/codex-usage":
            return self._send(404, {"error": "not_found"})
        # Header PRÓPRIO (não Authorization): o basic-auth do Traefik já ocupa o
        # Authorization com `Basic`. Dois portões independentes, sem colisão.
        if SHARED_TOKEN:
            got = self.headers.get("X-Bridge-Token", "")
            if got != SHARED_TOKEN:
                return self._send(401, {"error": "unauthorized"})
        data = get_usage()
        data["an"] = get_analytics()   # janelas + analytics num só payload
        return self._send(200, data)

    def log_message(self, *a):
        pass  # silencioso; o systemd/journald cuida do resto


def main():
    if not SHARED_TOKEN:
        print("[bridge] AVISO: CODEX_BRIDGE_TOKEN vazio — endpoint SEM autenticação")
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"[bridge] servindo em 127.0.0.1:{PORT} (cache {CACHE_SEC}s)")
    srv.serve_forever()


if __name__ == "__main__":
    import sys
    if "--once" in sys.argv:      # teste: imprime o JSON normalizado e sai
        data = get_usage(force=True)
        data["an"] = get_analytics(force=True)
        print(json.dumps(data, indent=2))
    else:
        main()
