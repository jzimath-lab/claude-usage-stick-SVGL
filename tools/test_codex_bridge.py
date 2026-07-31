#!/usr/bin/env python3
"""Testes de normalize_analytics — rodar: python3 tools/test_codex_bridge.py

Cobre os tres defeitos achados na auditoria de 2026-07-31 contra o payload real:

 1. Janelas misturadas: os agregados (interactions, credits_total, by_surface,
    by_model) vinham de AN_RANGE_DAYS=30 e o `daily` so dos ultimos
    AN_DAILY_KEEP=14. A tela "Interacoes" mostrava 2.280 sobre um grafico que
    somava 911 — os dois numeros certos, a tela mentindo.
 2. Corte silencioso: `_rank` cortava no top-5 e o excedente sumia. Medido:
    a origem `desktop_work` (3,4 creditos) e o modelo `5.4` (7 turns) fora.
 3. Total nao atribuido: a API devolve turns que nao aparecem em models[].
    Medido: 2.065 de 2.280. Os percentuais da tela somavam 90,5%.

O fio condutor dos tres: o que nao e mostrado tambem nao e sinalizado.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import codex_usage_bridge as b  # noqa: E402

FALHAS = []


def check(cond, msg):
    print(("  ok   " if cond else "  FALHA ") + msg)
    if not cond:
        FALHAS.append(msg)


def dia(data, credits, turns, clients, models):
    return {
        "date": data,
        "totals": {"credits": credits, "turns": turns, "threads": 1},
        "clients": [{"client_id": c, "credits": v} for c, v in clients],
        "models": [{"model": m, "turns": t} for m, t in models],
    }


def raw_sintetico():
    """30 dias. Os 16 mais antigos so tem CLI; os 14 recentes sao dominados
    por github — reproduz a inversao real de origem que o payload de producao
    sofreu entre 24/07 e 31/07."""
    dias = []
    for i in range(16):                                   # fora da janela do grafico
        dias.append(dia(f"2026-07-{i+1:02d}", 100, 10,
                        [("CODEX_CLI", 100)], [("gpt-5.6-luna", 10)]))
    for i in range(14):                                   # dentro da janela do grafico
        dias.append(dia(f"2026-07-{i+17:02d}", 200, 20,
                        [("CODEX_GITHUB", 150), ("CODEX_CLI", 20),
                         ("CODEX_WORK_WEB", 20), ("CODEX_DESKTOP_APP", 6),
                         ("CODEX_WORK_MOBILE", 3), ("CODEX_WORK_DESKTOP", 1)],
                        # 20 turns no total, mas so 15 atribuidos a modelo:
                        # reproduz a lacuna medida em producao
                        [("gpt-5.6-luna", 8), ("gpt-5.6-sol", 4),
                         ("gpt-5.5", 2), ("gpt-5.4", 1)]))
    return {"data": dias}


def main():
    an = b.normalize_analytics(raw_sintetico())
    daily = an["daily"]

    print("\n[1] uma janela so — agregados e grafico descrevem os mesmos dias")
    check(an["range_days"] == len(daily),
          f"range_days ({an['range_days']}) == dias no grafico ({len(daily)})")
    # Tolerâncias derivadas da aritmética, não ajustadas ao que se observou:
    # `credits` de cada dia é arredondado uma vez (erro <= 0,5), então a soma
    # de N dias erra no máximo 0,5·N contra o float `credits_total`. Dados
    # sintéticos inteiros escondem isso — o payload real de 2026-07-31 divergia
    # em 1,5 sobre 14 dias, dentro do limite.
    tol_dias = 0.5 * len(daily)
    check(abs(sum(d["credits"] for d in daily) - an["credits_total"]) <= tol_dias,
          f"soma daily.credits ({sum(d['credits'] for d in daily)}) "
          f"== credits_total ({an['credits_total']}) +/- {tol_dias}")
    check(sum(d["turns"] for d in daily) == an["interactions"],
          f"soma daily.turns ({sum(d['turns'] for d in daily)}) "
          f"== interactions ({an['interactions']})")

    print("\n[2] nada some em silencio — o que fica fora do top-N vira 'outros'")
    check(len(an["by_surface"]) <= b.AN_TOPN,
          f"by_surface cabe no top-N ({len(an['by_surface'])} <= {b.AN_TOPN})")
    soma_surf = sum(s["credits"] for s in an["by_surface"])
    check(abs(soma_surf - an["credits_total"]) < 0.5,
          f"soma by_surface ({soma_surf}) == credits_total ({an['credits_total']})")
    check(any(s["src"] == "outros" for s in an["by_surface"]),
          "existe linha 'outros' em by_surface (6 origens, top-N=5)")

    print("\n[3] parcela sem atribuicao aparece — nao vira percentual faltando")
    soma_mod = sum(m["turns"] for m in an["by_model"])
    check(abs(soma_mod - an["interactions"]) < 0.5,
          f"soma by_model ({soma_mod}) == interactions ({an['interactions']})")
    pct = sum(m["pct"] for m in an["by_model"])
    check(abs(pct - 100.0) < 1.0, f"percentuais de by_model somam ~100% ({pct})")

    print("\n[4] o grafico empilhado fecha com o total do dia")
    check(len(an["surf_order"]) == len(daily[0]["v"]),
          f"surf_order ({len(an['surf_order'])}) casa com v[] ({len(daily[0]['v'])})")
    # Cada origem do dia é arredondada uma vez, e o total do dia outra: o erro
    # máximo é 0,5·(origens + 1). Fora disso não é arredondamento, é fatia
    # perdida — que é o defeito que este teste existe para pegar.
    tol_v = 0.5 * (len(an["surf_order"]) + 1)
    ruins = [(d["d"], sum(d["v"]), d["credits"])
             for d in daily if abs(sum(d["v"]) - d["credits"]) > tol_v]
    check(not ruins, f"sum(v[]) == credits do dia +/- {tol_v} (fora: {ruins[:3]})")

    print("\n[5] regressao — a ordem das origens nao pode inverter a legenda")
    check(an["surf_order"][0] == "github",
          f"origem dominante na janela vem primeiro (veio: {an['surf_order'][0]})")

    print()
    if FALHAS:
        print(f"FALHOU: {len(FALHAS)} de {len(FALHAS) + 0} verificacoes quebradas")
        for f in FALHAS:
            print("  - " + f)
        return 1
    print("todos os testes passaram")
    return 0


if __name__ == "__main__":
    sys.exit(main())
