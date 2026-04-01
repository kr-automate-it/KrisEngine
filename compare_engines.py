#!/usr/bin/env python3
"""
compare_engines.py — Automatyczny match miedzy dwoma wersjami silnika.

Uzycie:
    python compare_engines.py --engine1 ./kris8.exe --engine2 ./krisLT.exe
    python compare_engines.py --engine1 ./kris8.exe --engine2 ./krisLT.exe --games 20 --tc 5000
    python compare_engines.py --engine1 ./kris8.exe --engine2 ./krisLT.exe --name1 "Stary" --name2 "Nowy"

Rozgrywa match z rowna liczba partii jako biale/czarne.
Wypisuje wynik, Elo difference, i statystyki.
"""

import argparse
import chess
import chess.engine
import math
import sys
import time


def play_game(engine1_path, engine2_path, tc_ms=5000, inc_ms=50,
              hash_mb=64, e1_options=None, e2_options=None):
    """
    Rozegraj 1 partie. engine1 = White, engine2 = Black.
    Zwraca (wynik_white, moves, reason).
    wynik_white: 1.0 / 0.5 / 0.0
    """
    board = chess.Board()

    e1 = chess.engine.SimpleEngine.popen_uci(engine1_path)
    e2 = chess.engine.SimpleEngine.popen_uci(engine2_path)

    e1.configure({"Hash": hash_mb, "OwnBook": True, **(e1_options or {})})
    e2.configure({"Hash": hash_mb, "OwnBook": True, **(e2_options or {})})

    wtime = tc_ms
    btime = tc_ms
    moves = 0

    try:
        while not board.is_game_over(claim_draw=True):
            if board.fullmove_number > 200:
                e1.quit()
                e2.quit()
                return 0.5, moves, "too_long"

            engine = e1 if board.turn == chess.WHITE else e2
            t_left = wtime if board.turn == chess.WHITE else btime

            if t_left <= 0:
                e1.quit()
                e2.quit()
                return (0.0 if board.turn == chess.WHITE else 1.0), moves, "time"

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
            moves += 1

            if board.turn == chess.BLACK:
                wtime -= elapsed_ms
                wtime += inc_ms
            else:
                btime -= elapsed_ms
                btime += inc_ms

    except Exception as e:
        print(f"  [ERROR] {e}")
        try: e1.quit()
        except: pass
        try: e2.quit()
        except: pass
        return 0.5, moves, "error"

    e1.quit()
    e2.quit()

    res = board.result(claim_draw=True)
    if res == "1-0":
        return 1.0, moves, "checkmate"
    elif res == "0-1":
        return 0.0, moves, "checkmate"
    else:
        return 0.5, moves, "draw"


def elo_diff(score, games):
    """Oblicz roznice Elo z wyniku matchu."""
    if games == 0:
        return 0
    win_rate = score / games
    if win_rate <= 0:
        return -999
    if win_rate >= 1:
        return 999
    return -400 * math.log10(1.0 / win_rate - 1.0)


def run_match(engine1, engine2, name1, name2, num_games, tc_ms, inc_ms, hash_mb,
              e1_options=None, e2_options=None):
    """Rozegraj match — polowa gier jako biale, polowa jako czarne."""
    pairs = num_games // 2
    score1 = 0.0
    score2 = 0.0
    wins1 = draws = wins2 = 0

    print(f"\n{'='*60}")
    print(f"Match: {name1} vs {name2}")
    print(f"Gier: {num_games} ({pairs} par), TC: {tc_ms}ms + {inc_ms}ms")
    print(f"{'='*60}\n")

    for i in range(pairs):
        # Partia 1: engine1 = White
        r, m, reason = play_game(engine1, engine2, tc_ms, inc_ms, hash_mb,
                                  e1_options, e2_options)
        score1 += r
        score2 += (1.0 - r)
        if r == 1.0: wins1 += 1
        elif r == 0.0: wins2 += 1
        else: draws += 1

        res_str = f"{name1}" if r == 1.0 else (f"{name2}" if r == 0.0 else "Remis")
        print(f"  Gra {i*2+1}: {name1}(W) vs {name2}(B) — {res_str} ({m} ruchow, {reason})")

        # Partia 2: engine2 = White
        r, m, reason = play_game(engine2, engine1, tc_ms, inc_ms, hash_mb,
                                  e2_options, e1_options)
        score2 += r
        score1 += (1.0 - r)
        if r == 0.0: wins1 += 1
        elif r == 1.0: wins2 += 1
        else: draws += 1

        res_str = f"{name1}" if r == 0.0 else (f"{name2}" if r == 1.0 else "Remis")
        print(f"  Gra {i*2+2}: {name2}(W) vs {name1}(B) — {res_str} ({m} ruchow, {reason})")

        total = (i + 1) * 2
        print(f"  >>> Wynik po {total} grach: {name1} {score1:.1f} - {score2:.1f} {name2}")
        print()

    # Podsumowanie
    total_games = pairs * 2
    elo = elo_diff(score1, total_games)

    print(f"{'='*60}")
    print(f"WYNIK KONCOWY:")
    print(f"{'='*60}")
    print(f"  {name1}: {score1:.1f}/{total_games}  (W:{wins1} D:{draws} L:{wins2})")
    print(f"  {name2}: {score2:.1f}/{total_games}")
    print(f"  Win rate {name1}: {score1/total_games*100:.1f}%")
    print(f"  Elo difference: {elo:+.0f} Elo ({name1} vs {name2})")
    print(f"{'='*60}")

    return score1, score2, elo


def main():
    parser = argparse.ArgumentParser(description="Compare two chess engines")
    parser.add_argument("--engine1", required=True, help="Pierwszy silnik (bazowy)")
    parser.add_argument("--engine2", required=True, help="Drugi silnik (nowy)")
    parser.add_argument("--name1", default="Engine1", help="Nazwa silnika 1")
    parser.add_argument("--name2", default="Engine2", help="Nazwa silnika 2")
    parser.add_argument("--games", type=int, default=20, help="Liczba gier (parzysta)")
    parser.add_argument("--tc", type=int, default=5000, help="Czas na partie (ms)")
    parser.add_argument("--inc", type=int, default=50, help="Inkrement (ms)")
    parser.add_argument("--hash", type=int, default=64, help="Hash MB")
    parser.add_argument("--threads1", type=int, default=1, help="Threads silnika 1")
    parser.add_argument("--threads2", type=int, default=1, help="Threads silnika 2")
    args = parser.parse_args()

    if args.games % 2 != 0:
        args.games += 1

    e1_opts = {"Threads": args.threads1} if args.threads1 > 1 else {}
    e2_opts = {"Threads": args.threads2} if args.threads2 > 1 else {}

    run_match(
        args.engine1, args.engine2,
        args.name1, args.name2,
        args.games, args.tc, args.inc, args.hash,
        e1_opts, e2_opts,
    )


if __name__ == "__main__":
    main()
