# Estação — `GET /cotas` (ZYN-568 + ZYN-569)

LAN collector for the 3.5″ stick. Advertises **`estacao.local`** (the inverse of
`claude-stick.local`). The ESP32 only paints `QuotaSnapshot` JSON. It never talks
to the GitHub API and never opens cookies / `state.vscdb` / `auth.json` / JSONL.

**Real sources on this slice:** GitHub Actions (G1) and Codex. Cursor / Gemini
stay `no_source` until later issues.

## Run

```bash
cd estacao
cp .env.example .env   # fill G1_* and/or Codex paths — no secrets in git
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

### Codex (station only)

Preference, in order:

1. `CODEXBAR_URL` — existing `codexbar serve`, `GET /usage?provider=codex`
2. `codexbar` on `PATH` — `codexbar usage --format json --provider codex`
3. `~/.codex/auth.json` or `$CODEX_HOME/auth.json` →
   `GET https://chatgpt.com/backend-api/wham/usage`
4. `codex` on `PATH` — RPC `codex -s read-only -a never app-server` →
   `account/rateLimits/read`

No chatgpt.com scrape. Token stays on the station; we never write it back
to `auth.json` and never put it in git. Missing `usedPercent` / `used_percent`
stays omitted (`SEM FONTE`), never a fake 0%.

## Verify

**Station up — Actions**

```bash
curl -s http://estacao.local:8787/cotas | jq '.sources[] | select(.source=="actions")'
```

Expect `windows[0].usedAbsolute` (minutes this month) and `windows[1].usedAbsolute`
(USD due) from G1. `usedPct` present only when G1 sent it.

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
PORT=8787 node estacao/server/index.js
curl -s http://127.0.0.1:8787/cotas | jq '.sources[] | select(.source=="codex" or .source=="actions")'
# Codex 5h usedPct=28 resetAt=2026-08-31T19:15:00.000Z; Actions minutos=731
```

**Station down** — stop this process. On the stick: Claude (tile 1) keeps
updating from unified headers; tiles 2–5 show `STALE` / `SEM FONTE`, never 0%.
