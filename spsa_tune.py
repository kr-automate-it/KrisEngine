#!/usr/bin/env python3
"""
SPSA tuner for ChessCpp.
Simultaneous Perturbation Stochastic Approximation — automatyczne strojenie wag silnika.

Uzycie:
    python spsa_tune.py                  # domyslne ustawienia
    python spsa_tune.py --games 50       # wiecej gier na iteracje (dokladniej, wolniej)
    python spsa_tune.py --iterations 200 # wiecej iteracji
    python spsa_tune.py --resume         # wznow z ostatniego zapisanego stanu
"""

import chess.engine
import random
import math
import json
import os
import sys
import time
import argparse
from dataclasses import dataclass
from pathlib import Path

# === Konfiguracja parametrow do tuningu ===
# (nazwa UCI, wartosc startowa, min, max, delta startowe)
# Delta = rozmiar perturbacji. Wieksze = szybszy ale mniej dokladny tuning.

@dataclass
class TuneParam:
    name: str
    value: float
    min_val: float
    max_val: float
    delta: float  # c_k w SPSA — perturbacja

PARAMS = [
    # Mobilnosc
    TuneParam("knightMobMG",  4,  0, 10, 1.0),
    TuneParam("knightMobEG",  3,  0, 10, 1.0),
    TuneParam("bishopMobMG",  5,  0, 10, 1.0),
    TuneParam("bishopMobEG",  4,  0, 10, 1.0),
    TuneParam("rookMobMG",    2,  0, 10, 0.5),
    TuneParam("rookMobEG",    3,  0, 10, 0.5),
    TuneParam("queenMobMG",   1,  0,  5, 0.5),
    TuneParam("queenMobEG",   1,  0,  5, 0.5),

    # Passed pawns
    TuneParam("passedPawnScale", 100, 50, 200, 10),

    # Bishop pair
    TuneParam("bishopPairMG", 30,  0, 80, 5),
    TuneParam("bishopPairEG", 50,  0, 100, 5),

    # Pawn structure
    TuneParam("isolatedPawnMG", 10, 0, 30, 3),
    TuneParam("isolatedPawnEG", 15, 0, 30, 3),
    TuneParam("doubledPawnMG",   5, 0, 20, 2),
    TuneParam("doubledPawnEG",  10, 0, 20, 2),

    # Knight outpost
    TuneParam("knightOutpostMG", 25, 0, 50, 5),
    TuneParam("knightOutpostEG", 15, 0, 30, 5),

    # Rook
    TuneParam("rookOpenFileMG",  20, 0, 40, 5),
    TuneParam("rookOpenFileEG",  15, 0, 30, 5),
    TuneParam("rook7thRankMG",   20, 0, 40, 5),
    TuneParam("rook7thRankEG",   30, 0, 50, 5),

    # Tempo
    TuneParam("tempoMG", 15, 0, 30, 3),
    TuneParam("tempoEG",  5, 0, 15, 2),

    # Search
    TuneParam("aspirationWindow",      25, 10, 100, 5),
    TuneParam("nullMoveBaseR",          3,  2,   5, 0.5),
    TuneParam("futilityMargin",       200, 50, 400, 20),
    TuneParam("reverseFutilityMargin",120, 50, 250, 15),
    TuneParam("razorMarginBase",      300,100, 500, 25),
    TuneParam("razorMarginPerDepth",  200, 50, 400, 20),
    TuneParam("lmpBase",                3,  1,   8, 1),
]


def make_options(params: list[TuneParam], perturbation: list[int] | None = None) -> dict:
    """Zamien parametry na dict opcji UCI. perturbation: lista +1/-1 na parametr."""
    opts = {}
    for i, p in enumerate(params):
        val = p.value
        if perturbation is not None:
            val += perturbation[i] * p.delta
        # Clamp do zakresu i zaokraglij do int (UCI wymaga int)
        val = max(p.min_val, min(p.max_val, val))
        opts[p.name] = int(round(val))
    return opts


def play_game(engine_path: str, opts_white: dict, opts_black: dict,
              tc_ms: int = 5000, inc_ms: int = 50, hash_mb: int = 32) -> float:
    """
    Rozegraj 1 partie miedzy dwiema konfiguracjami silnika.
    Zwraca wynik z perspektywy WHITE: 1.0 / 0.5 / 0.0
    """
    board = chess.Board()

    white = chess.engine.SimpleEngine.popen_uci(engine_path)
    black = chess.engine.SimpleEngine.popen_uci(engine_path)

    # Ustaw opcje
    white.configure({"Hash": hash_mb, "OwnBook": False, **opts_white})
    black.configure({"Hash": hash_mb, "OwnBook": False, **opts_black})

    wtime = tc_ms
    btime = tc_ms

    try:
        while not board.is_game_over(claim_draw=True):
            if board.fullmove_number > 200:
                white.quit()
                black.quit()
                return 0.5  # za dluga partia = remis

            engine = white if board.turn == chess.WHITE else black
            t_left = wtime if board.turn == chess.WHITE else btime

            if t_left <= 0:
                # Przegrana na czas
                white.quit()
                black.quit()
                return 0.0 if board.turn == chess.WHITE else 1.0

            limit = chess.engine.Limit(
                white_clock=wtime / 1000.0,
                black_clock=btime / 1000.0,
                white_inc=inc_ms / 1000.0,
                black_inc=inc_ms / 1000.0,
            )

            start = time.perf_counter()
            result = engine.play(board, limit)
            elapsed_ms = int((time.perf_counter() - start) * 1000)

            if result.move is None:
                break

            board.push(result.move)

            # Aktualizuj zegar
            if board.turn == chess.BLACK:  # bialy dopiero zagral
                wtime -= elapsed_ms
                wtime += inc_ms
            else:
                btime -= elapsed_ms
                btime += inc_ms

    except Exception as e:
        print(f"  [ERROR] Partia przerwana: {e}")
        try: white.quit()
        except: pass
        try: black.quit()
        except: pass
        return 0.5

    white.quit()
    black.quit()

    result_str = board.result(claim_draw=True)
    if result_str == "1-0":
        return 1.0
    elif result_str == "0-1":
        return 0.0
    else:
        return 0.5


def play_match(engine_path: str, opts_plus: dict, opts_minus: dict,
               num_games: int, tc_ms: int, inc_ms: int) -> tuple[float, float]:
    """
    Rozegraj match: opts_plus vs opts_minus.
    Kazda para gier: raz plus=white, raz plus=black (eliminuje przewage bialych).
    Zwraca (score_plus, score_minus) — suma punktow.
    """
    score_plus = 0.0
    score_minus = 0.0
    pairs = num_games // 2

    for i in range(pairs):
        # Partia 1: plus = white
        r = play_game(engine_path, opts_plus, opts_minus, tc_ms, inc_ms)
        score_plus += r
        score_minus += (1.0 - r)

        # Partia 2: plus = black
        r = play_game(engine_path, opts_minus, opts_plus, tc_ms, inc_ms)
        score_minus += r
        score_plus += (1.0 - r)

        total = (i + 1) * 2
        print(f"  Gry {total}/{num_games}: plus={score_plus:.1f} minus={score_minus:.1f}")

    return score_plus, score_minus


def spsa_tune(engine_path: str, iterations: int = 100, games_per_iter: int = 24,
              tc_ms: int = 5000, inc_ms: int = 50, resume: bool = False):
    """
    Glowna petla SPSA.

    Algorytm:
    1. Dla kazdego parametru losuj kierunek (+1 lub -1)
    2. Stworz params+ (value + delta * direction) i params- (value - delta * direction)
    3. Rozegraj match params+ vs params-
    4. Gradient ≈ (score+ - score-) / (2 * delta * direction)
    5. Aktualizuj: value += a_k * gradient
    6. Zmniejszaj a_k i c_k w czasie (annealing)
    """

    params = [TuneParam(p.name, p.value, p.min_val, p.max_val, p.delta) for p in PARAMS]

    state_file = Path("spsa_state.json")
    log_file = Path("spsa_log.txt")

    start_iter = 0

    # Wznowienie z zapisanego stanu
    if resume and state_file.exists():
        with open(state_file) as f:
            state = json.load(f)
        for p in params:
            if p.name in state["params"]:
                p.value = state["params"][p.name]
        start_iter = state.get("iteration", 0)
        print(f"Wznawiam od iteracji {start_iter}, zaladowane {len(state['params'])} parametrow")

    # SPSA hyperparameters
    a = 5.0       # learning rate (bazowy)
    A = 20.0      # stabilizacja (wieksze A = wolniejszy start, stabilniejszy)
    alpha = 0.602  # wykladnik learning rate
    gamma = 0.101  # wykladnik perturbacji

    print(f"\n{'='*60}")
    print(f"SPSA Tuning — {len(params)} parametrow")
    print(f"Iteracje: {iterations}, gier/iter: {games_per_iter}")
    print(f"TC: {tc_ms}ms + {inc_ms}ms inc")
    print(f"{'='*60}\n")

    # Wypisz startowe wartosci
    print("Startowe wartosci:")
    for p in params:
        print(f"  {p.name:25s} = {p.value:8.1f}  [{p.min_val}-{p.max_val}]  delta={p.delta}")
    print()

    for k in range(start_iter, iterations):
        # Annealing: zmniejszaj learning rate i perturbacje w czasie
        a_k = a / (A + k + 1) ** alpha
        c_k_scale = 1.0 / (k + 1) ** gamma

        # 1. Losowy kierunek Bernoulli (+1/-1)
        direction = [random.choice([-1, 1]) for _ in params]

        # 2. Perturbacja
        opts_plus = {}
        opts_minus = {}
        for i, p in enumerate(params):
            c_k = p.delta * c_k_scale
            val_plus = max(p.min_val, min(p.max_val, p.value + direction[i] * c_k))
            val_minus = max(p.min_val, min(p.max_val, p.value - direction[i] * c_k))
            opts_plus[p.name] = int(round(val_plus))
            opts_minus[p.name] = int(round(val_minus))

        print(f"--- Iteracja {k+1}/{iterations} (a_k={a_k:.4f}, c_scale={c_k_scale:.3f}) ---")

        # 3. Rozegraj match
        score_plus, score_minus = play_match(
            engine_path, opts_plus, opts_minus, games_per_iter, tc_ms, inc_ms
        )

        # Wynik znormalizowany: roznica score / liczba gier -> [-1, 1]
        total_games = games_per_iter
        result_diff = (score_plus - score_minus) / total_games  # [-1, 1]

        # 4. Aktualizuj parametry
        changes = []
        for i, p in enumerate(params):
            c_k = p.delta * c_k_scale
            if c_k < 0.001:
                continue
            # Gradient SPSA: g_k = result_diff / (2 * c_k * direction)
            # Ale direction jest +/-1, wiec: g_k = result_diff * direction / (2 * c_k)
            gradient = result_diff * direction[i] / (2.0 * c_k)
            step = a_k * gradient

            old_val = p.value
            p.value += step
            p.value = max(p.min_val, min(p.max_val, p.value))
            changes.append((p.name, old_val, p.value))

        # 5. Loguj
        print(f"  Wynik: plus={score_plus:.1f} minus={score_minus:.1f} diff={result_diff:+.3f}")
        top_changes = sorted(changes, key=lambda x: abs(x[2] - x[1]), reverse=True)[:5]
        for name, old, new in top_changes:
            if abs(new - old) > 0.01:
                print(f"  {name}: {old:.1f} -> {new:.1f}")
        print()

        # 6. Zapisz stan co iteracje
        state = {
            "iteration": k + 1,
            "params": {p.name: round(p.value, 2) for p in params},
        }
        with open(state_file, "w") as f:
            json.dump(state, f, indent=2)

        # Log do pliku
        with open(log_file, "a") as f:
            f.write(f"iter={k+1} diff={result_diff:+.3f} "
                    + " ".join(f"{p.name}={p.value:.1f}" for p in params) + "\n")

    # Wypisz finalne wartosci
    print(f"\n{'='*60}")
    print("FINALNE WARTOSCI (do wklejenia w params.h):")
    print(f"{'='*60}")
    for p in params:
        orig = next(op for op in PARAMS if op.name == p.name)
        diff = p.value - orig.value
        marker = " *" if abs(diff) > orig.delta else ""
        print(f"  {p.name:25s} = {int(round(p.value)):4d}  (bylo {int(round(orig.value)):4d}, zmiana {diff:+.1f}){marker}")

    # Zapisz gotowy kod C++
    with open("params_tuned.h", "w") as f:
        f.write("// Wygenerowane przez SPSA tuner\n")
        f.write("// Wklej wartosci do params.h\n\n")
        for p in params:
            f.write(f"    int {p.name} = {int(round(p.value))};\n")

    print(f"\nZapisano do params_tuned.h")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SPSA tuner for ChessCpp")
    parser.add_argument("--engine", default="./chesscpp_v5.exe", help="Sciezka do silnika")
    parser.add_argument("--iterations", type=int, default=100, help="Liczba iteracji SPSA")
    parser.add_argument("--games", type=int, default=24, help="Gier na iteracje (parzyste!)")
    parser.add_argument("--tc", type=int, default=5000, help="Czas na partie w ms")
    parser.add_argument("--inc", type=int, default=50, help="Inkrement w ms")
    parser.add_argument("--resume", action="store_true", help="Wznow z spsa_state.json")
    args = parser.parse_args()

    if args.games % 2 != 0:
        args.games += 1
        print(f"Zaokraglono gry do {args.games} (musi byc parzyste)")

    spsa_tune(
        engine_path=args.engine,
        iterations=args.iterations,
        games_per_iter=args.games,
        tc_ms=args.tc,
        inc_ms=args.inc,
        resume=args.resume,
    )
