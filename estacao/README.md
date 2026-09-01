# Estação — `GET /cotas` (ZYN-568)

LAN collector for the 3.5″ stick. Advertises **`estacao.local`** (the inverse of
`claude-stick.local`). The ESP32 only paints `QuotaSnapshot` JSON. It never talks
to the GitHub API and never opens cookies / `state.vscdb` / `auth.json` / JSONL.

This slice’s only real source is **GitHub Actions**, reused from G1
(`estacao/server/services/github.js` + `github-painel.js` in the mesa-station
tree / `estacao-server`). CodexBar does not cover Actions. Codex / Cursor /
Gemini stay `no_source` until later issues.

## Run

```bash
cd estacao
cp .env.example .env   # fill G1_URL or G1_DIR — no secrets in git
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

## Verify

**Station up**

```bash
curl -s http://estacao.local:8787/cotas | jq '.sources[] | select(.source=="actions")'
```

Expect `windows[0].usedAbsolute` (minutes this month) and `windows[1].usedAbsolute`
(USD due) from G1. `usedPct` present only when G1 sent it.

**Station down** — stop this process. On the stick: Claude (tile 1) keeps
updating from unified headers; tiles 2–5 show `STALE` / `SEM FONTE`, never 0%.
