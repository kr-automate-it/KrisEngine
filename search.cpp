#include "search.h"
#include "movegen.h"
#include "tt.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

// === Tabela LMR ===
// lmrTable[depth][moveNumber] = redukcja
// Oparta na log(depth) * log(moveNumber) — precyzyjniejsza niz reczne progi.
static int lmrTable[64][64];

static void init_lmr() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++) {
            if (d == 0 || m == 0)
                lmrTable[d][m] = 0;
            else
                lmrTable[d][m] = (int)(0.5 + std::log(d) * std::log(m) / 2.0);
        }
}

// === Killer moves ===
// Ruchy ktore spowodowaly beta cutoff na danej glebokosci.
// Nie sa biciami, ale sa dobre — sortujemy je wysoko.
// Trzymamy 2 na kazdy ply (nowy wypycha starego).
static Move killers[128][2];

static void store_killer(int ply, Move m) {
    if (m != killers[ply][0]) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
}

// === History heuristic ===
// Statystyki: ile razy cichy ruch spowodowal cutoff.
// Im wyzszy score, tym wyzej w sortowaniu.
static int history[COLOR_NB][SQUARE_NB][SQUARE_NB];

static void update_history(Color c, Move m, int depth) {
    history[c][move_from(m)][move_to(m)] += depth * depth;
    if (history[c][move_from(m)][move_to(m)] > 1000000) {
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                history[c][f][t] /= 2;
    }
}

// === Countermove heuristic ===
// Dla kazdego ruchu przeciwnika (piece, to_sq), pamietamy jaki nasz ruch go obalil.
// Jezeli przeciwnik zagral Nf3, a my odpowiedzielismy d5 i to dalo cutoff,
// to nastepnym razem gdy przeciwnik zagra Nf3, prubujemy d5 wczesniej.
static Move countermoves[PIECE_NB][SQUARE_NB];

// === Kontrola czasu ===

static bool time_up(SearchInfo& info) {
    if (info.timeLimit <= 0) return false;
    if (info.nodes % 2048 != 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - info.startTime).count();
    return elapsed >= info.timeLimit;
}

// === Sortowanie ruchow ===
// Kolejnosc: TT move > bicia (MVV-LVA) > killer moves > history

static const int SCORE_TT_MOVE      = 10000000;
static const int SCORE_GOOD_CAPTURE = 5000000;
static const int SCORE_KILLER1      = 900000;
static const int SCORE_KILLER2      = 800000;
static const int SCORE_COUNTERMOVE  = 700000;

static int move_score(const Position& pos, Move m, Move ttMove, int ply, Move counterMove) {
    if (m == ttMove)
        return SCORE_TT_MOVE;

    Piece captured = pos.piece_on(move_to(m));
    Piece mover    = pos.piece_on(move_from(m));

    // Bicia: MVV-LVA (szybkie, bez SEE)
    if (captured != NO_PIECE)
        return SCORE_GOOD_CAPTURE + PieceValue[type_of(captured)] * 10 - PieceValue[type_of(mover)];

    if (move_type(m) == PROMOTION)
        return SCORE_GOOD_CAPTURE + PieceValue[promo_type(m)];

    if (ply < 128) {
        if (m == killers[ply][0]) return SCORE_KILLER1;
        if (m == killers[ply][1]) return SCORE_KILLER2;
    }

    if (m == counterMove)
        return SCORE_COUNTERMOVE;

    return history[pos.side_to_move()][move_from(m)][move_to(m)];
}

static void sort_moves(const Position& pos, MoveList& list, Move ttMove, int ply, Move counterMove) {
    int scores[256];
    for (int i = 0; i < list.count; i++)
        scores[i] = move_score(pos, list.moves[i], ttMove, ply, counterMove);

    for (int i = 0; i < list.count - 1; i++) {
        int best = i;
        for (int j = i + 1; j < list.count; j++) {
            if (scores[j] > scores[best])
                best = j;
        }
        if (best != i) {
            std::swap(list.moves[i], list.moves[best]);
            std::swap(scores[i], scores[best]);
        }
    }
}

// === Quiescence Search ===
// Przeszukuje bicia az do pozycji "cichej" (bez wisiacych bic).
// Bez tego silnik moze myslec ze pozycja jest dobra, a tak naprawde
// traci figure w nastepnym ruchu.

static int quiescence(Position& pos, int alpha, int beta, SearchInfo& info, int ply) {
    if (info.stopped.load(std::memory_order_relaxed)) return 0;
    if (ply > 64) return evaluate(pos);

    info.nodes++;

    int stand_pat = evaluate(pos);
    if (stand_pat >= beta)
        return beta;
    if (stand_pat > alpha)
        alpha = stand_pat;

    MoveList list;
    generate_captures(pos, list);
    sort_moves(pos, list, MOVE_NONE, ply, MOVE_NONE);

    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];

        if (!pos.is_legal(m))
            continue;

        // Delta pruning
        Piece cap = pos.piece_on(move_to(m));
        if (cap != NO_PIECE && stand_pat + PieceValue[type_of(cap)] + 200 < alpha
            && move_type(m) != PROMOTION)
            continue;

        StateInfo newSt;
        pos.do_move(m, newSt);
        int score = -quiescence(pos, -beta, -alpha, info, ply + 1);
        pos.undo_move(m);

        if (score >= beta)
            return beta;
        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

// === Glowne przeszukiwanie Alpha-Beta ===

static int alpha_beta(Position& pos, int depth, int alpha, int beta,
                       SearchInfo& info, int ply, bool doNull,
                       Move prevMove = MOVE_NONE) {
    if (info.stopped.load(std::memory_order_relaxed) || time_up(info)) {
        info.stopped.store(true, std::memory_order_relaxed);
        return 0;
    }

    // Detekcja remisu
    if (ply > 0 && pos.is_draw())
        return VALUE_DRAW;

    // === Mate Distance Pruning ===
    // Jesli nawet mat na nastepnym ruchu nie poprawilby alpha, odetnij.
    // Jesli juz znalezlismy mata w N ruchach, nie szukaj dluższych.
    if (ply > 0) {
        int matingScore = VALUE_MATE - ply;
        if (matingScore < beta) {
            beta = matingScore;
            if (alpha >= matingScore)
                return matingScore;
        }
        int matedScore = -VALUE_MATE + ply;
        if (matedScore > alpha) {
            alpha = matedScore;
            if (beta <= matedScore)
                return matedScore;
        }
    }

    // Wejscie do quiescence na koncu glebokosci
    if (depth <= 0)
        return quiescence(pos, alpha, beta, info, ply);

    info.nodes++;
    bool inCheck = pos.in_check();

    // Check extension na wejsciu — jestesmy w szachu
    if (inCheck)
        depth++;

    bool pvNode = (beta - alpha > 1); // czy to okno PV?

    // === Probe tablicy transpozycji ===
    Move ttMove = MOVE_NONE;
    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);

    if (ttHit) {
        ttMove = tte->move;

        // Uzywamy TT jesli glebokosc wystarczajaca i to nie PV node
        if (tte->depth >= depth && !pvNode) {
            int ttScore = tte->score;

            if (tte->flag == TT_EXACT)
                return ttScore;
            if (tte->flag == TT_BETA && ttScore >= beta)
                return ttScore;
            if (tte->flag == TT_ALPHA && ttScore <= alpha)
                return ttScore;
        }
    }

    int eval = evaluate(pos);

    // === Reverse Futility Pruning (Static Null Move Pruning) ===
    // Jesli statyczna ewaluacja jest duzo ponad beta, odetnij od razu.
    if (!inCheck && !pvNode && depth <= 3 && eval - 120 * depth >= beta)
        return eval;

    // === Razoring ===
    // Jesli eval jest DALEKO pod alpha na malej glebokosci, to nawet najlepszy
    // ruch pewnie nie pomoze. Skocz prosto do qsearch — jesli potwierdzi, odetnij.
    if (!inCheck && !pvNode && depth <= 2) {
        int razorMargin = 300 + 200 * depth;
        if (eval + razorMargin < alpha) {
            int qScore = quiescence(pos, alpha, beta, info, ply);
            if (qScore < alpha)
                return qScore;
        }
    }

    // === Null Move Pruning ===
    if (doNull && !inCheck && !pvNode && depth >= 3 && eval >= beta) {
        U64 nonPawnMaterial = pos.pieces(pos.side_to_move())
            & ~pos.pieces(PAWN) & ~pos.pieces(KING);

        if (nonPawnMaterial) {
            int R = 3 + depth / 6;

            StateInfo newSt;
            pos.do_null_move(newSt);
            int score = -alpha_beta(pos, depth - R - 1, -beta, -beta + 1,
                                     info, ply + 1, false);
            pos.undo_null_move();

            if (info.stopped.load(std::memory_order_relaxed))
                return 0;

            if (score >= beta)
                return beta;
        }
    }

    // Probcut wylaczony — zbyt kosztowny w obecnej implementacji

    // === Internal Iterative Deepening (IID) ===
    if (pvNode && ttMove == MOVE_NONE && depth >= 4) {
        alpha_beta(pos, depth - 2, alpha, beta, info, ply, false, prevMove);
        bool iidHit;
        TTEntry* iidEntry = TT.probe(pos.key(), iidHit);
        if (iidHit) ttMove = iidEntry->move;
    }

    // === Generuj i przeszukuj ruchy ===
    MoveList list;
    generate_moves(pos, list);
    // Countermove: jaki ruch obalil poprzedni ruch przeciwnika?
    Move counterMove = MOVE_NONE;
    if (prevMove != MOVE_NONE) {
        Piece prevPc = pos.piece_on(move_to(prevMove)); // figura ktora sie ruszyla (teraz stoi na to)
        if (prevPc != NO_PIECE)
            counterMove = countermoves[prevPc][move_to(prevMove)];
    }
    sort_moves(pos, list, ttMove, ply, counterMove);

    int legalMoves = 0;
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INFINITE;
    TTFlag ttFlag = TT_ALPHA;

    // Sledzenie cichych ruchow do history malus
    Move quietsTried[64];
    int quietsTriedCount = 0;

    // Futility pruning margin: na malych glebokosciach, jesli eval + margines < alpha,
    // to ciche ruchy nie maja szansy poprawic alpha — pomijamy je.
    bool canFutility = !inCheck && !pvNode && depth <= 3
                       && eval + 200 * depth < alpha;

    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];

        if (!pos.is_legal(m))
            continue;

        legalMoves++;
        bool isCapture = pos.piece_on(move_to(m)) != NO_PIECE || move_type(m) == EN_PASSANT;
        bool isPromo   = move_type(m) == PROMOTION;
        bool isQuiet   = !isCapture && !isPromo;

        if (isQuiet && quietsTriedCount < 64)
            quietsTried[quietsTriedCount++] = m;

        // === Late Move Pruning (LMP) ===
        if (!pvNode && !inCheck && isQuiet && depth <= 3 && legalMoves > 3 + depth * depth)
            continue;

        // History pruning wyłączony (brak negatywnej historii)

        // === Futility Pruning ===
        if (canFutility && legalMoves > 1 && isQuiet)
            continue;

        // SEE Pruning wyłączony (zbyt kosztowny per-move)

        // === Extensions ===
        int extension = 0;

        // Passed pawn push extension: TYLKO na 7. rzad (promocja bliska)
        if (type_of(pos.piece_on(move_from(m))) == PAWN) {
            int pushRank = (pos.side_to_move() == WHITE) ? rank_of(move_to(m)) : 7 - rank_of(move_to(m));
            if (pushRank >= 6) extension = 1;
        }

        StateInfo newSt;
        pos.do_move(m, newSt);

        bool givesCheck = pos.in_check();
        int score;

        // Check extension
        if (givesCheck)
            extension = std::max(extension, 1);

        int newDepth = depth - 1 + extension;

        // === PVS + LMR ===
        if (legalMoves == 1) {
            score = -alpha_beta(pos, newDepth, -beta, -alpha, info, ply + 1, true, m);
        } else {
            // === LMR ===
            int reduction = 0;
            if (legalMoves > 3 && depth >= 3 && !inCheck && !givesCheck && isQuiet) {
                reduction = 1;
                if (legalMoves > 8)  reduction++;
                if (legalMoves > 16) reduction++;
                if (!pvNode) reduction++;
                // History korekta
                int hist = history[pos.side_to_move()][move_from(m)][move_to(m)];
                if (hist > 5000) reduction--;
                reduction = std::max(0, std::min(reduction, depth - 2));
            }

            // Zero-window search (PVS)
            score = -alpha_beta(pos, newDepth - reduction, -alpha - 1, -alpha,
                                 info, ply + 1, true, m);

            if (score > alpha && reduction > 0) {
                score = -alpha_beta(pos, newDepth, -alpha - 1, -alpha,
                                     info, ply + 1, true, m);
            }

            if (score > alpha && score < beta) {
                score = -alpha_beta(pos, newDepth, -beta, -alpha, info, ply + 1, true, m);
            }
        }

        pos.undo_move(m);

        if (info.stopped.load(std::memory_order_relaxed))
            return 0;

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }

        if (score >= beta) {
            ttFlag = TT_BETA;
            if (isQuiet) {
                store_killer(ply, m);
                update_history(pos.side_to_move(), m, depth);
                if (prevMove != MOVE_NONE) {
                    Piece prevPc = pos.piece_on(move_to(prevMove));
                    if (prevPc != NO_PIECE)
                        countermoves[prevPc][move_to(prevMove)] = m;
                }
            }
            break;
        }

        if (score > alpha) {
            alpha = score;
            ttFlag = TT_EXACT;
        }
    }

    // Mat lub pat
    if (legalMoves == 0) {
        if (inCheck)
            return -VALUE_MATE + ply;
        return VALUE_DRAW;
    }

    // Zapisz do tablicy transpozycji
    TT.store(pos.key(), bestScore, depth, bestMove, ttFlag);

    return bestScore;
}

// === Iterative Deepening — glowna petla ===

SearchResult search(Position& pos, SearchInfo& info) {
    SearchResult result;
    result.bestMove = MOVE_NONE;
    result.score = -VALUE_INFINITE;

    info.startTime = std::chrono::steady_clock::now();
    info.nodes = 0;
    info.stopped.store(false);

    static bool lmrInit = false;
    if (!lmrInit) { init_lmr(); lmrInit = true; }

    std::memset(killers, 0, sizeof(killers));
    std::memset(countermoves, 0, sizeof(countermoves));

    // Iterative deepening: szukaj od depth 1 w gore
    // Kazda iteracja korzysta z TT wypelnionej przez poprzednia
    for (int depth = 1; depth <= info.maxDepth; depth++) {
        info.depth = depth;

        // === Aspiration windows ===
        // Zamiast szukac z oknem (-INF, +INF), zawezamy okno wokol
        // wyniku z poprzedniej iteracji. Jezeli wynik miesci sie w oknie
        // — oszczedzamy czas. Jezeli nie — poszerzamy okno i szukamy ponownie.
        int alpha, beta;
        if (depth >= 4 && std::abs(result.score) < VALUE_MATE - 100) {
            alpha = result.score - 50;
            beta  = result.score + 50;
        } else {
            alpha = -VALUE_INFINITE;
            beta  =  VALUE_INFINITE;
        }

        int score;
        while (true) {
            score = alpha_beta(pos, depth, alpha, beta, info, 0, true);

            if (info.stopped.load(std::memory_order_relaxed))
                break;

            // Fail-low: wynik ponizej okna — poszerz w dol
            if (score <= alpha) {
                alpha = -VALUE_INFINITE;
                continue;
            }
            // Fail-high: wynik powyzej okna — poszerz w gore
            if (score >= beta) {
                beta = VALUE_INFINITE;
                continue;
            }
            break; // Wynik miesci sie w oknie
        }

        if (info.stopped.load(std::memory_order_relaxed))
            break;

        // Pobierz najlepszy ruch z TT
        bool ttHit;
        TTEntry* tte = TT.probe(pos.key(), ttHit);
        if (ttHit && tte->move != MOVE_NONE)
            result.bestMove = tte->move;
        result.score = score;

        // Wypisz info UCI z pelna linia PV (odczytana z TT)
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - info.startTime).count();
        int64_t nps = (elapsed > 0) ? (info.nodes * 1000 / elapsed) : info.nodes;

        // Odczytaj PV z tablicy transpozycji
        std::string pvStr;
        StateInfo pvStates[64];
        Move pvMoves[64];
        int pvLen = 0;
        for (int i = 0; i < depth + 10 && i < 64; i++) {
            bool hit;
            TTEntry* e = TT.probe(pos.key(), hit);
            if (!hit || e->move == MOVE_NONE) break;
            Move pvMove = e->move;
            if (!pos.is_legal(pvMove)) break;
            pvStr += pos.move_to_uci(pvMove) + " ";
            pvMoves[pvLen] = pvMove;
            pos.do_move(pvMove, pvStates[i]);
            pvLen++;
        }
        for (int i = pvLen - 1; i >= 0; i--)
            pos.undo_move(pvMoves[i]);

        std::cout << "info depth " << depth
                  << " score cp " << score
                  << " nodes " << info.nodes
                  << " nps " << nps
                  << " time " << elapsed
                  << " pv " << pvStr
                  << std::endl;

        if (std::abs(score) > VALUE_MATE - 100)
            break;
    }

    return result;
}
