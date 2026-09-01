# Estação — `GET /cotas` (ZYN-568 + ZYN-569 + ZYN-570 + review 573/574/575)

LAN collector for the 3.5″ stick. Advertises **`_http._tcp` instance `estacao`**
(bonjour-service publishes `MDNS_NAME` as the instance; the machine hostname
may be anything — `queryHost("estacao")` does not create `estacao.local`).
Copy `estacao/.env` before start: `PORT` / `HOST` / `MDNS_NAME` / `POLL_MS`
are read after `.env` loads. The ESP32 only paints `QuotaSnapshot` JSON. It
never talks to the GitHub API and never opens cookies / `state.vscdb` /
`auth.json` / JSONL.

**Real sources on this slice:** GitHub Actions (G1), Codex, and Cursor / Grok Bot.
Gemini stays `no_source` until the probe.

## Run

```bash
cd estacao
cp .env.example .env   # fill G1_*, Codex, and/or CURSOR_COOKIE — no secrets in git
npm install
npm test
npm start              # GET http://<lan>:8787/cotas
```

### Point at the live G1 collector (preferred)

G1 already exposes `GET /api/github` on `estacao-server` (device token, not
`PAINEL_TOKEN`). The stick never hits that URL — only this process does:

```
G1_URL=http://127.0.0.1:3010/api/github
G1_DEVICE_TOKEN=...
```

### Or require the G1 module

If a checkout of G1 is on disk, we `require` `server/services/github.js` and
call `buscarBilling`. We do **not** copy that client.

```
G1_DIR=/docker/estacao
GITHUB_TOKEN=...          # Plan:Read
GITHUB_USER=...
GH_INCLUIDOS=2000
```

Without either, Actions is honest `no_source` (never a fake 0%).

### Cursor / Grok Bot (station only)

Preference, in order:

1. `codexbar` on `PATH` — `codexbar usage --format json --provider cursor`
2. `CODEXBAR_URL` — existing `codexbar serve`, `GET /usage?provider=cursor`
3. Closed list (do not invent endpoints):
   - Token: `cursorAuth/accessToken` in Cursor `state.vscdb` (macOS default;
     `CURSOR_VSCDB` / `CURSOR_TOKEN` override)
   - Cookie: `WorkosCursorSessionToken` / `__Secure-next-auth.session-token` /
     `next-auth.session-token` via **`CURSOR_COOKIE`** (pasted)
   - `GET https://cursor.com/api/usage-summary` (included vs on-demand)
   - Grok Bot weekly: `POST https://cursor.com/api/dashboard/get-sand-usage-status`
     with `Origin: https://cursor.com`

**Linux = pasted cookie.** This process never auto-imports Chrome / Firefox /
Safari cookie DBs. Grok Bot is appended only when `usagePercent` is in the JSON
(including a measured `0`). A failed POST does not zero the monthly bars.
Omitted `usedPct` / `usagePercent` stays omitted (`SEM FONTE`), never a fake 0%.

Out of v1: `get-filtered-usage-events`, screen scrape, CodexBar Add/Switch Account.

### Codex (station only)

Preference, in order:

1. `CODEXBAR_URL` — existing `codexbar serve`, `GET /usage?provider=codex`
2. `codexbar` on `PATH` — `codexbar usage --format json --provider codex`
3. `~/.codex/auth.json` or `$CODEX_HOME/auth.json` →
   `GET https://chatgpt.com/backend-api/wham/usage`
4. `codex` on `PATH` — RPC `codex -s read-only -a never app-server` →
   `account/rateLimits/read`

A preferred collector that returns HTTP 200 / CLI exit 0 with empty,
structured-error, or omitted `usedPercent` is `no_source` and **does not
return immediately** — the chain continues. Measured `0%` stays `0%` and
does not fall through. Missing `usedPercent` / `used_percent` stays omitted
(`SEM FONTE`), never a fake 0%. No chatgpt.com scrape. Token stays on the
station; we never write it back to `auth.json` and never put it in git.

## Verify

**Station up — Actions**

```bash
curl -s http://estacao.local:8787/cotas | jq '.sources[] | select(.source=="actions")'
```

Expect `windows[0].usedAbsolute` (minutes this month) and `windows[1].usedAbsolute`
(USD due) from G1. `usedPct` present only when G1 sent it.

**Station up — Cursor**

```bash
curl -s http://estacao.local:8787/cotas | jq '.sources[] | select(.source=="cursor")'
```

| Setup | Expect |
| --- | --- |
| `codexbar` on PATH, Cursor logged in | `windows[0]` included `usedPct`, `windows[1]` on-demand |
| `CODEXBAR_URL` pointing at `codexbar serve` | same |
| macOS `state.vscdb` (or `CURSOR_TOKEN`) | same, via `usage-summary` |
| `CURSOR_COOKIE` pasted (Linux / any OS) | same |
| Grok `usagePercent` in the sand JSON | `windows[2]` `grok_bot` |
| sand POST fails or field missing | monthly bars stay; no `grok_bot` window |
| none of the above | both windows `status: no_source`, no `usedPct` (`SEM FONTE`) |

**Station up — Codex**

```bash
curl -s http://estacao.local:8787/cotas | jq '.sources[] | select(.source=="codex")'
```

| Setup | Expect |
| --- | --- |
| `codexbar` on PATH, logged in | `windows[0].usedPct` (5h) + `resetAt`, `windows[1]` weekly |
| `CODEXBAR_URL` pointing at `codexbar serve` | same |
| `~/.codex/auth.json` (or `$CODEX_HOME`) | same, via `wham/usage` |
| `codex` CLI only | same, via app-server RPC |
| none of the above | both windows `status: no_source`, no `usedPct` (`SEM FONTE`) |

Fixture-only (no live creds):

```bash
G1_FIXTURE=estacao/test/fixtures/g1-github.json \
CODEX_FIXTURE=estacao/test/fixtures/codexbar-usage.json \
CURSOR_FIXTURE=estacao/test/fixtures/cursor-usage.json \
PORT=8787 node estacao/server/index.js
curl -s http://127.0.0.1:8787/cotas | jq '.sources[] | select(.source=="cursor" or .source=="codex" or .source=="actions")'
# Cursor incluido=41 on_demand=21 grok_bot=12; Codex 5h=28; Actions minutos=731
```

**Station down** — stop this process. On the stick: Claude (tile 1) keeps
updating from unified headers; tiles 2–5 show `STALE` / `SEM FONTE`, never 0%.
