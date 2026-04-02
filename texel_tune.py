#!/usr/bin/env python3
"""
Texel Tuning for ChessCpp.

Metoda Texela: optymalizuj parametry eval tak, zeby statyczna ewaluacja
jak najlepiej przewidywala wynik partii.

Uzycie:
    python texel_tune.py --pgn games.pgn
    python texel_tune.py --pgn games.pgn --engine ./krisLT.exe --positions 50000
    python texel_tune.py --pgn "C:/Program Files (x86)/Arena/Tournaments/*.pgn"

Jak dziala:
1. Wyciaga pozycje z PGN (pomija otwarcie i pozycje w szachu)
2. Dla kazdej pozycji: eval silnika + wynik partii (1.0 / 0.5 / 0.0)
3. Minimalizuje: E = sum( (result - sigmoid(eval * K))^2 ) / N
4. Local search: zmienia parametry jeden po jednym, szukajac minimum bledu
"""

import argparse
import chess
import chess.pgn
import chess.engine
import glob
import json
import math
import os
import re
import sys
import time
from pathlib import Path


def read_uci_options(engine_path):
    """Czyta opcje UCI z silnika (nazwa -> {default, min, max})."""
    import subprocess
    proc = subprocess.Popen(
        [engine_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True
    )
    proc.stdin.write("uci\n")
    proc.stdin.flush()
    options = {}
    while True:
        line = proc.stdout.readline().strip()
        if line == "uciok":
            break
        m = re.match(
            r'option name (\S+) type spin default (\d+) min (\d+) max (\d+)',
            line
        )
        if m:
            options[m.group(1)] = {
                "default": int(m.group(2)),
                "min": int(m.group(3)),
                "max": int(m.group(4)),
            }
    proc.stdin.write("quit\n")
    proc.stdin.flush()
    proc.wait()
    return options


# Parametry eval do tuningu + krok (min/max/default brane z silnika UCI)
EVAL_PARAMS = {
    "knightMobMG":    1,
    "knightMobEG":    1,
    "bishopMobMG":    1,
    "bishopMobEG":    1,
    "rookMobMG":      1,
    "rookMobEG":      1,
    "queenMobMG":     1,
    "queenMobEG":     1,
    "passedPawnScale": 5,
    "bishopPairMG":   5,
    "bishopPairEG":   5,
    "isolatedPawnMG": 2,
    "isolatedPawnEG": 2,
    "doubledPawnMG":  2,
    "doubledPawnEG":  2,
    "knightOutpostMG": 5,
    "knightOutpostEG": 5,
    "rookOpenFileMG":  3,
    "rookOpenFileEG":  3,
    "rook7thRankMG":   5,
    "rook7thRankEG":   5,
    "backwardPawnMG":  2,
    "backwardPawnEG":  2,
    "connectedPawnMG": 1,
    "connectedPawnEG": 2,
    "badBishopMG":     1,
    "badBishopEG":     1,
    "kingCentralEG":   2,
    "hangingPieceMG":  3,
    "hangingPieceEG":  3,
    "tempoMG":        2,
    "tempoEG":        2,
}


def build_params(engine_path):
    """Buduje liste PARAMS z UCI opcji silnika + krokow z EVAL_PARAMS."""
    options = read_uci_options(engine_path)
    params = []
    for name, step in EVAL_PARAMS.items():
        opt = options.get(name)
        if opt is None:
            print(f"  UWAGA: {name} nie znaleziony w silniku UCI, pomijam")
            continue
        params.append((name, opt["default"], opt["min"], opt["max"], step))
    return params


def sigmoid(x, K=1.13):
    """Sigmoid: eval (cp) -> oczekiwany wynik [0,1]."""
    return 1.0 / (1.0 + math.pow(10.0, -K * x / 400.0))


def extract_positions(pgn_paths, max_positions=50000, skip_plies=16):
    """Wyciaga pozycje z PGN. Zwraca [(fen, result), ...]"""
    positions = []
    game_count = 0

    for pgn_path in pgn_paths:
        print(f"  Czytam: {pgn_path}")
        with open(pgn_path) as f:
            while True:
                game = chess.pgn.read_game(f)
                if game is None:
                    break

                result_str = game.headers.get("Result", "*")
                if result_str == "1-0":
                    result = 1.0
                elif result_str == "0-1":
                    result = 0.0
                elif result_str == "1/2-1/2":
                    result = 0.5
                else:
                    continue

                board = game.board()
                ply = 0
                for move in game.mainline_moves():
                    board.push(move)
                    ply += 1
                    if ply < skip_plies:
                        continue
                    if board.is_check() or board.is_game_over():
                        continue
                    if len(board.piece_map()) <= 4:
                        continue
                    positions.append((board.fen(), result))
                    if len(positions) >= max_positions:
                        break

                game_count += 1
                if len(positions) >= max_positions:
                    break

        if len(positions) >= max_positions:
            break

    print(f"Wyciagnieto {len(positions)} pozycji z {game_count} gier")
    return positions


def evaluate_positions(engine_path, positions, params_dict, depth=1):
    """Ewaluuje pozycje silnikiem. Zwraca [eval_cp, ...]"""
    try:
        engine = chess.engine.SimpleEngine.popen_uci(engine_path)
        engine.configure({"OwnBook": False, "Hash": 16, **params_dict})
    except Exception as e:
        print(f"  [ERROR] Nie mozna uruchomic silnika: {e}")
        return None

    evals = []
    errors = 0
    start_time = time.time()
    for i, (fen, _) in enumerate(positions):
        try:
            board = chess.Board(fen)
            info = engine.analyse(board, chess.engine.Limit(depth=depth, time=2.0))
            score = info["score"].white()
            cp = score.score(mate_score=10000) if not score.is_mate() else (10000 if score.mate() > 0 else -10000)
            evals.append(cp)
        except Exception as e:
            errors += 1
            evals.append(0)  # fallback
            if errors <= 3:
                print(f"  [WARN] Pozycja {i+1} error: {e}")
            if errors > 10:
                print(f"  [ERROR] Za duzo bledow ({errors}), przerywam")
                try: engine.quit()
                except: pass
                return None

        if (i + 1) % 5000 == 0:
            elapsed = time.time() - start_time
            rate = (i + 1) / elapsed
            eta = (len(positions) - i - 1) / rate
            print(f"    Eval: {i+1}/{len(positions)}  ({rate:.0f} poz/s, ETA {eta:.0f}s, errors={errors})")

    try:
        engine.quit()
    except:
        pass

    if errors > 0:
        print(f"  Zakonczono z {errors} bledami")
    return evals


def mse(evals, results, K=1.13):
    """Mean Squared Error."""
    return sum((r - sigmoid(e, K)) ** 2 for e, r in zip(evals, results)) / len(evals)


def find_K(evals, results):
    """Znajdz optymalne K."""
    best_K, best_err = 1.0, float('inf')
    for k in range(50, 200):
        K = k / 100.0
        err = mse(evals, results, K)
        if err < best_err:
            best_err = err
            best_K = K
    print(f"Optymalne K = {best_K:.2f} (MSE = {best_err:.6f})")
    return best_K


def texel_tune(engine_path, positions, iterations=5, depth=1):
    """Local Search Texel Tuning."""
    PARAMS = build_params(engine_path)
    current = {p[0]: p[1] for p in PARAMS}
    results = [r for _, r in positions]

    print(f"\n{'='*60}")
    print(f"Texel Tuning — {len(PARAMS)} parametrow, {len(positions)} pozycji")
    print(f"{'='*60}\n")

    # Baseline
    print("Baseline eval...")
    base_evals = evaluate_positions(engine_path, positions, current, depth)
    if base_evals is None:
        print("[FATAL] Nie udalo sie obliczyc baseline eval. Sprawdz silnik.")
        return current
    K = find_K(base_evals, results)
    base_error = mse(base_evals, results, K)
    print(f"Baseline MSE: {base_error:.6f}\n")

    import random as _rnd
    for it in range(iterations):
        improved = False
        print(f"--- Iteracja {it+1}/{iterations} (MSE = {base_error:.6f}) ---")

        # Losowa kolejnosc parametrow — eliminuje bias
        param_order = list(PARAMS)
        _rnd.shuffle(param_order)

        for name, default, lo, hi, step in param_order:
            old_val = current[name]
            best_val = old_val
            best_err = base_error

            for direction in [+step, -step]:
                new_val = old_val + direction
                if new_val < lo or new_val > hi:
                    continue

                current[name] = new_val
                new_evals = evaluate_positions(engine_path, positions, current, depth)
                if new_evals is None:
                    print(f"  [ERROR] Silnik padl przy {name}={new_val}, cofam")
                    current[name] = old_val
                    continue
                new_err = mse(new_evals, results, K)

                if new_err < best_err:
                    best_err = new_err
                    best_val = new_val
                    base_evals = new_evals

                    # Kontynuuj w tym kierunku az przestanie sie poprawiac
                    while True:
                        next_val = best_val + direction
                        if next_val < lo or next_val > hi:
                            break
                        current[name] = next_val
                        next_evals = evaluate_positions(engine_path, positions, current, depth)
                        if next_evals is None:
                            break
                        next_err = mse(next_evals, results, K)
                        if next_err < best_err:
                            best_err = next_err
                            best_val = next_val
                            base_evals = next_evals
                        else:
                            break

            current[name] = best_val
            if best_val != old_val:
                improved = True
                base_error = best_err
                print(f"  {name}: {old_val} -> {best_val}  (MSE {base_error:.6f})")

        if not improved:
            print("Brak poprawy — koniec.")
            break

        with open("texel_state.json", "w") as f:
            json.dump({"iteration": it + 1, "mse": base_error, "params": current}, f, indent=2)
        print(f"MSE po iteracji: {base_error:.6f}\n")

    # Wynik
    print(f"\n{'='*60}")
    print("FINALNE WARTOSCI:")
    print(f"{'='*60}")
    for name, default, lo, hi, step in PARAMS:
        val = current[name]
        diff = val - default
        marker = " *" if diff != 0 else ""
        print(f"  {name:25s} = {val:4d}  (bylo {default:4d}, zmiana {diff:+d}){marker}")

    with open("params_texel.h", "w") as f:
        f.write("// Wygenerowane przez Texel tuner\n\n")
        for name, *_ in PARAMS:
            f.write(f"    int {name} = {current[name]};\n")

    print(f"\nZapisano do params_texel.h")
    return current


def main():
    parser = argparse.ArgumentParser(description="Texel Tuner for ChessCpp")
    parser.add_argument("--engine", default="./krisLT.exe")
    parser.add_argument("--pgn", type=str, default="")
    parser.add_argument("--positions", type=int, default=30000)
    parser.add_argument("--depth", type=int, default=1)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--skip-plies", type=int, default=16)
    args = parser.parse_args()

    # Znajdz PGN
    pgn_files = []
    if args.pgn:
        pgn_files = glob.glob(args.pgn)
    if not pgn_files:
        # Szukaj automatycznie
        pgn_files = glob.glob("*.pgn") + glob.glob("C:/Program Files (x86)/Arena/Tournaments/*.pgn")

    if not pgn_files:
        print("Brak PGN. Podaj: --pgn games.pgn")
        sys.exit(1)

    print(f"Znalezione PGN ({len(pgn_files)}):")
    for f in pgn_files[:10]:
        print(f"  {f}")

    positions = extract_positions(pgn_files, args.positions, args.skip_plies)

    if len(positions) < 100:
        print(f"Za malo pozycji ({len(positions)}). Potrzeba min 1000+.")
        sys.exit(1)

    texel_tune(args.engine, positions, args.iterations, args.depth)


if __name__ == "__main__":
    main()
