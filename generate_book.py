"""
generate_book.py — Generuje książkę Polyglot .bin używając Stockfisha.

BFS po drzewie otwarć: Stockfish ocenia top N ruchów w każdej pozycji,
wynik zapisujemy jako Polyglot .bin (natywny format naszego silnika).

Użycie:
  python generate_book.py
  python generate_book.py --depth 14 --ply 20 --width 3 --max-positions 5000
"""

import argparse
import math
import os
import struct
import time
from collections import deque

import chess
import chess.engine
import chess.polyglot

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

_SEED_LINES = [
    [],
    ["e2e4"], ["d2d4"], ["c2c4"], ["g1f3"],
    ["e2e4", "e7e5"], ["e2e4", "c7c5"], ["e2e4", "e7e6"], ["e2e4", "c7c6"],
    ["d2d4", "d7d5"], ["d2d4", "g8f6"],
    ["d2d4", "d7d5", "c2c4"],
    ["e2e4", "e7e5", "g1f3"],
    ["e2e4", "e7e5", "g1f3", "b8c6"],
    ["e2e4", "e7e5", "g1f3", "b8c6", "f1c4"],
    ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5"],
    ["e2e4", "c7c5", "g1f3", "d7d6"],
    ["e2e4", "c7c5", "g1f3", "b8c6"],
    ["d2d4", "g8f6", "c2c4", "e7e6"],
    ["d2d4", "g8f6", "c2c4", "g7g6"],
]


def encode_polyglot_move(move):
    """Koduj chess.Move w format Polyglot (16 bit)."""
    to_file = chess.square_file(move.to_square)
    to_row = chess.square_rank(move.to_square)
    from_file = chess.square_file(move.from_square)
    from_row = chess.square_rank(move.from_square)
    promo = 0
    if move.promotion:
        promo = {chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4}[move.promotion]
    # Roszada: Polyglot koduje jako krol -> wieza
    return to_file | (to_row << 3) | (from_file << 6) | (from_row << 9) | (promo << 12)


def cp_to_weight(cp):
    """Zamień centipawny na weight (0-1000). Lepszy ruch = wyższy weight."""
    # Sigmoid: 50% przy cp=0, ~90% przy cp=200
    prob = 1.0 / (1.0 + math.exp(-0.004 * cp))
    return max(1, int(prob * 1000))


def generate(sf_path, output, sf_depth, max_ply, width, min_cp, max_positions, verbose):
    engine = chess.engine.SimpleEngine.popen_uci(sf_path)
    engine.configure({"Threads": 2, "Hash": 128})

    # key -> {encoded_move: weight}
    book_data = {}
    visited = set()
    queue = deque()

    for line in _SEED_LINES:
        queue.append(line)

    processed = 0
    t0 = time.time()

    while queue and processed < max_positions:
        moves_uci = queue.popleft()

        board = chess.Board()
        ok = True
        for uci in moves_uci:
            try:
                board.push_uci(uci)
            except Exception:
                ok = False
                break
        if not ok or board.is_game_over():
            continue

        zh = chess.polyglot.zobrist_hash(board)
        if zh in visited:
            continue
        visited.add(zh)

        if board.ply() >= max_ply:
            continue

        try:
            info_list = engine.analyse(board, chess.engine.Limit(depth=sf_depth), multipv=width)
        except Exception:
            continue

        processed += 1

        for info in info_list:
            pv = info.get("pv", [])
            score = info.get("score")
            if not pv or score is None:
                continue

            move = pv[0]
            score_rel = score.relative

            if not score_rel.is_mate():
                cp = score_rel.score(mate_score=10000)
                if cp < min_cp:
                    continue
            else:
                cp = 10000 if score_rel.mate() > 0 else -10000

            em = encode_polyglot_move(move)
            weight = cp_to_weight(cp)

            if zh not in book_data:
                book_data[zh] = {}
            # Zachowaj najwyższy weight
            if em not in book_data[zh] or weight > book_data[zh][em]:
                book_data[zh][em] = weight

            # Rozwijaj drzewo
            next_line = moves_uci + [move.uci()]
            if len(pv) > 1:
                next_line.append(pv[1].uci())
            if len(next_line) <= max_ply:
                queue.append(next_line)

        if verbose and processed % 50 == 0:
            elapsed = time.time() - t0
            print(f"  [{elapsed:6.1f}s] pozycji: {processed:5d}  w kolejce: {len(queue):5d}")

    engine.quit()

    # Zapisz jako Polyglot .bin
    entries = []
    for key, moves in book_data.items():
        for em, weight in moves.items():
            entries.append((key, em, weight))

    entries.sort(key=lambda x: x[0])

    with open(output, "wb") as f:
        for key, move, weight in entries:
            f.write(struct.pack(">QHHi", key, move, weight, 0))

    elapsed = time.time() - t0
    print(f"\nGotowe w {elapsed:.1f}s")
    print(f"  Pozycji: {len(book_data)}")
    print(f"  Ruchów:  {len(entries)}")
    print(f"  Plik:    {output} ({os.path.getsize(output)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Generuj Polyglot book Stockfishem")
    parser.add_argument("--stockfish", default=None)
    parser.add_argument("--output", default=os.path.join(BASE_DIR, "book.bin"))
    parser.add_argument("--depth", type=int, default=14)
    parser.add_argument("--ply", type=int, default=20)
    parser.add_argument("--width", type=int, default=3)
    parser.add_argument("--min-cp", type=float, default=-50)
    parser.add_argument("--max-positions", type=int, default=2000)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    # Znajdz Stockfisha
    sf = args.stockfish
    if not sf:
        for name in ["stockfish.exe", "stockfish"]:
            p = os.path.join(BASE_DIR, name)
            if os.path.isfile(p):
                sf = p
                break
    if not sf:
        print("Nie znaleziono Stockfisha!")
        return

    print(f"Stockfish: {sf}")
    print(f"Output:    {args.output}")
    print(f"Depth: {args.depth}  Ply: {args.ply}  Width: {args.width}  Max: {args.max_positions}")
    print()

    generate(sf, args.output, args.depth, args.ply, args.width,
             args.min_cp, args.max_positions, not args.quiet)


if __name__ == "__main__":
    main()
