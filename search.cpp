#include "search.h"
#include "movegen.h"
#include "tt.h"
#include "params.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <thread>

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
                lmrTable[d][m] = (int)(0.5 + std::log(d) * std::log(m) / 2.25);
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

// === Countermove history (follow-up history) ===
// Tablica indeksowana przez (prevPiece, prevTo) → (from, to) score.
// Kompaktowa wersja: [PIECE_NB][SQUARE_NB] → tablica [64][64] = ~4MB total
static int cmHistory[PIECE_NB][SQUARE_NB][SQUARE_NB]; // [prevPiece][prevTo][currTo]

static void update_cm_history(Piece prevPc, Square prevTo, Move m, int bonus) {
    if (prevPc == NO_PIECE) return;
    int& val = cmHistory[prevPc][prevTo][move_to(m)];
    // Gravity: val = val * (1 - |bonus|/512) + bonus
    val += bonus - val * std::abs(bonus) / 512;
}

static int get_cm_history(Piece prevPc, Square prevTo, Move m) {
    if (prevPc == NO_PIECE) return 0;
    return cmHistory[prevPc][prevTo][move_to(m)];
}

// === Capture history ===
// Statystyki bic: [atakujacy_piece][to_sq][typ_bitej_figury] -> score.
// Ulepszenie MVV-LVA — uczy sie ktore bicia sa dobre w danym kontekscie.
static int captureHistory[PIECE_NB][SQUARE_NB][PIECE_TYPE_NB];

static void update_capture_history(Piece pc, Square to, PieceType captured, int bonus) {
    int& val = captureHistory[pc][to][captured];
    // Gravity (jak w cmHistory)
    val += bonus - val * std::abs(bonus) / 512;
}

// === Countermove heuristic ===
// Dla kazdego ruchu przeciwnika (piece, to_sq), pamietamy jaki nasz ruch go obalil.
static Move countermoves[PIECE_NB][SQUARE_NB];

// === Kontrola czasu ===

static bool time_up(SearchInfo& info) {
    if (info.timeLimit <= 0) return false;
    if ((info.nodes.load(std::memory_order_relaxed) & 1023) != 0) return false;
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

static int move_score(const Position& pos, Move m, Move ttMove, int ply,
                      Move counterMove, Piece prevPc, Square prevTo) {
    if (m == ttMove)
        return SCORE_TT_MOVE;

    Piece captured = pos.piece_on(move_to(m));
    Piece mover    = pos.piece_on(move_from(m));

    if (captured != NO_PIECE) {
        int capHist = captureHistory[mover][move_to(m)][type_of(captured)];
        return SCORE_GOOD_CAPTURE + PieceValue[type_of(captured)] * 10 - PieceValue[type_of(mover)] + capHist / 16;
    }

    if (move_type(m) == PROMOTION)
        return SCORE_GOOD_CAPTURE + PieceValue[promo_type(m)];

    if (ply < 128) {
        if (m == killers[ply][0]) return SCORE_KILLER1;
        if (m == killers[ply][1]) return SCORE_KILLER2;
    }

    if (m == counterMove)
        return SCORE_COUNTERMOVE;

    // Kombinacja history + countermove history
    int hist = history[pos.side_to_move()][move_from(m)][move_to(m)];
    int cmHist = get_cm_history(prevPc, prevTo, m);
    return hist + cmHist;
}

static void sort_moves(const Position& pos, MoveList& list, Move ttMove, int ply,
                       Move counterMove, Piece prevPc = NO_PIECE, Square prevTo = SQ_NONE) {
    int scores[256];
    for (int i = 0; i < list.count; i++)
        scores[i] = move_score(pos, list.moves[i], ttMove, ply, counterMove, prevPc, prevTo);

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

    bool inCheck = pos.in_check();
    if (ply > (inCheck ? 80 : 64)) return evaluate(pos);

    info.nodes.fetch_add(1, std::memory_order_relaxed);

    // === #1: TT probe w qsearch ===
    Move ttMove = MOVE_NONE;
    bool ttHit;
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    if (ttHit) {
        ttMove = tte->move;
        if (tte->depth >= 0) { // depth 0 = qsearch depth
            int ttScore = tte->score;
            if (tte->flag == TT_EXACT) return ttScore;
            if (tte->flag == TT_BETA && ttScore >= beta) return ttScore;
            if (tte->flag == TT_ALPHA && ttScore <= alpha) return ttScore;
        }
    }

    int stand_pat = inCheck ? -VALUE_INFINITE : evaluate(pos);

    if (!inCheck) {
        if (stand_pat >= beta)
            return stand_pat; // #2: fail-soft (zwracaj stand_pat nie beta)
        if (stand_pat > alpha)
            alpha = stand_pat;
    }

    int bestScore = stand_pat;
    Move bestMove = MOVE_NONE;

    MoveList list;
    if (inCheck)
        generate_moves(pos, list);
    else
        generate_captures(pos, list);

    // MVV-LVA scoring
    int scores[256];
    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        if (m == ttMove) { scores[i] = 10000000; continue; } // TT move first
        Piece cap = pos.piece_on(move_to(m));
        Piece mover = pos.piece_on(move_from(m));
        if (cap != NO_PIECE && mover != NO_PIECE)
            scores[i] = PieceValue[type_of(cap)] * 10 - PieceValue[type_of(mover)];
        else if (move_type(m) == PROMOTION)
            scores[i] = PieceValue[promo_type(m)] * 10;
        else
            scores[i] = 0;
    }

    int legalMoves = 0;
    for (int i = 0; i < list.count; i++) {
        // Lazy pick
        int best = i;
        for (int j = i + 1; j < list.count; j++)
            if (scores[j] > scores[best]) best = j;
        if (best != i) {
            std::swap(list.moves[i], list.moves[best]);
            std::swap(scores[i], scores[best]);
        }

        Move m = list.moves[i];
        if (!pos.is_legal(m)) continue;
        legalMoves++;

        if (!inCheck) {
            Piece cap = pos.piece_on(move_to(m));
            if (cap != NO_PIECE && stand_pat + PieceValue[type_of(cap)] + 250 < alpha
                && move_type(m) != PROMOTION)
                continue;
            if (scores[i] < -100)
                continue;
        }

        StateInfo newSt;
        pos.do_move(m, newSt);
        int score = -quiescence(pos, -beta, -alpha, info, ply + 1);
        pos.undo_move(m);

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }

        // #2: fail-soft
        if (score >= beta) {
            // #3: TT store
            TT.store(pos.key(), score, 0, m, TT_BETA, stand_pat);
            return score;
        }
        if (score > alpha)
            alpha = score;
    }

    if (inCheck && legalMoves == 0)
        return -VALUE_MATE + ply;

    // #3: TT store
    TTFlag flag = (bestScore > alpha) ? TT_EXACT : TT_ALPHA;
    TT.store(pos.key(), bestScore, 0, bestMove, flag, stand_pat);

    return bestScore; // #2: fail-soft
}

// === Glowne przeszukiwanie Alpha-Beta ===

static int alpha_beta(Position& pos, int depth, int alpha, int beta,
                       SearchInfo& info, int ply, bool doNull,
                       Move prevMove = MOVE_NONE, Move excludedMove = MOVE_NONE) {
    if (info.stopped.load(std::memory_order_relaxed) || time_up(info)) {
        info.stopped.store(true, std::memory_order_relaxed);
        return 0;
    }

    // Detekcja remisu — z contempt (unikaj remisow, graj na wynik)
    if (ply > 0 && pos.is_draw())
        return -10; // lekki contempt: remis = -10cp (woli grac dalej niz remisowac)

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

    info.nodes.fetch_add(1, std::memory_order_relaxed);
    bool inCheck = pos.in_check();

    // Check extension na wejsciu — twardy limit na ply
    if (inCheck && ply < 80)
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

    // Eval — uzyj cached z TT jesli dostepny
    int eval;
    if (inCheck) {
        eval = -VALUE_INFINITE;
    } else if (ttHit && tte->flag != TT_NONE) {
        eval = tte->staticEval; // cached — oszczedza evaluate()
    } else {
        eval = evaluate(pos);
    }

    // Zapisz eval w StateInfo — uzywane do improving heuristic
    // (nie mamy dostępu do st bezpośrednio, ale pos ma current state)
    // Trick: pos.key() jest unikalne, ale potrzebujemy st->staticEval.
    // Zrobimy to inaczej: trzymamy eval w tablicy po ply.
    static thread_local int evalHistory[128];
    if (ply < 128 && !inCheck) evalHistory[ply] = eval;

    // Improving: czy nasza pozycja jest lepsza niż 2 ply temu?
    bool improving = (!inCheck && ply >= 2 && ply < 128 && eval > evalHistory[ply - 2]);

    // === Reverse Futility Pruning (Static Null Move Pruning) ===
    // Jesli statyczna ewaluacja jest duzo ponad beta, odetnij od razu.
    // Margines mniejszy gdy improving (ostrozniej), wiekszy gdy nie (agresywniej)
    int rfMargin = params.reverseFutilityMargin * depth - (improving ? 0 : 50);
    if (!inCheck && !pvNode && depth <= 3 && eval - rfMargin >= beta)
        return eval;

    // === Razoring ===
    // Jesli eval jest DALEKO pod alpha na malej glebokosci, to nawet najlepszy
    // ruch pewnie nie pomoze. Skocz prosto do qsearch — jesli potwierdzi, odetnij.
    if (!inCheck && !pvNode && depth <= 2) {
        int razorMargin = params.razorMarginBase + params.razorMarginPerDepth * depth;
        if (eval + razorMargin < alpha) {
            int qScore = quiescence(pos, alpha - 1, alpha, info, ply);
            if (qScore < alpha)
                return qScore;
        }
    }

    // === Null Move Pruning ===
    // Wylaczony w koncowkach z niskim materialem (zugzwang risk)
    if (doNull && !inCheck && !pvNode && depth >= 3 && eval >= beta) {
        U64 nonPawnMaterial = pos.pieces(pos.side_to_move())
            & ~pos.pieces(PAWN) & ~pos.pieces(KING);
        // Minimum: wiecej niz 1 lekka figura (sam krol+pionki = zugzwang terytory)
        if (popcount(nonPawnMaterial) >= 2) {
            int R = params.nullMoveBaseR + depth / 4 + std::min((eval - beta) / 200, 3);

            StateInfo newSt;
            pos.do_null_move(newSt);
            int score = -alpha_beta(pos, depth - R - 1, -beta, -beta + 1,
                                     info, ply + 1, false);
            pos.undo_null_move();

            if (info.stopped.load(std::memory_order_relaxed))
                return 0;

            if (score >= beta) {
                // Verification search: na duzych glebokosciach, sprawdz
                // czy wynik nie jest spowodowany zugzwangiem.
                // Przeszukaj z doNull=false (bez kolejnego null move).
                if (depth >= 8) {
                    int vScore = alpha_beta(pos, depth - R - 1, beta - 1, beta,
                                             info, ply, false, prevMove);
                    if (info.stopped.load(std::memory_order_relaxed))
                        return 0;
                    if (vScore >= beta)
                        return beta;
                    // Weryfikacja nie potwierdzila — kontynuuj normalny search
                } else {
                    return beta;
                }
            }
        }
    }

    // === ProbCut ===
    // Jesli shallow search z podwyzszonym beta daje cutoff,
    // to pelny search tez prawdopodobnie da cutoff — odetnij wczesnie.
    if (!pvNode && !inCheck && depth >= 5 && std::abs(beta) < VALUE_MATE - 100) {
        int probBeta = beta + 100;

        MoveList probList;
        generate_captures(pos, probList);
        // Prosty MVV-LVA sort — bez killerow/historii (to tylko bicia)
        for (int i = 0; i < probList.count - 1; i++) {
            int bestVal = -1, bestIdx = i;
            for (int j = i; j < probList.count; j++) {
                Piece cap = pos.piece_on(move_to(probList.moves[j]));
                int val = (cap != NO_PIECE) ? PieceValue[type_of(cap)] : 0;
                if (val > bestVal) { bestVal = val; bestIdx = j; }
            }
            if (bestIdx != i) std::swap(probList.moves[i], probList.moves[bestIdx]);
        }

        for (int i = 0; i < probList.count; i++) {
            Move pm = probList.moves[i];
            if (!pos.is_legal(pm)) continue;

            // Tylko bicia z dobrym SEE
            if (pos.see(pm) < 0) continue;

            StateInfo probSt;
            pos.do_move(pm, probSt);
            int probScore = -alpha_beta(pos, depth - 4, -probBeta, -probBeta + 1,
                                         info, ply + 1, true, pm);
            pos.undo_move(pm);

            if (info.stopped.load(std::memory_order_relaxed))
                return 0;

            if (probScore >= probBeta)
                return probScore;
        }
    }

    // === Internal Iterative Deepening (IID) ===
    if (pvNode && ttMove == MOVE_NONE && depth >= 6) {
        alpha_beta(pos, depth / 2, alpha, beta, info, ply, false, prevMove);
        if (info.stopped.load(std::memory_order_relaxed)) return 0;
        bool iidHit;
        TTEntry* iidEntry = TT.probe(pos.key(), iidHit);
        if (iidHit) ttMove = iidEntry->move;
    }

    // === Internal Iterative Reduction (IIR) ===
    // Brak TT move na duzej glebokosci = nie mamy dobrego kandydata.
    // Zmniejsz depth o 1 — szybciej zapelni TT na nastepnej iteracji.
    if (ttMove == MOVE_NONE && depth >= 4 && !pvNode)
        depth--;

    // === Singular Extensions ===
    // Jesli TT move jest dużo lepszy od alternatyw — extend go o 1 ply.
    // Robi to ogromna roznice w taktyce (unikanie przeoczeń).
    int singularExt = 0;
    if (depth >= 6 && ttMove != MOVE_NONE && !pvNode && ttHit
        && tte->depth >= depth - 3
        && (tte->flag == TT_BETA || tte->flag == TT_EXACT)
        && std::abs(tte->score) < VALUE_MATE - 100
        && excludedMove == MOVE_NONE) {
        int singularBeta = tte->score - depth * 2;
        int singularScore = alpha_beta(pos, depth / 2, singularBeta - 1, singularBeta,
                                        info, ply, false, prevMove, ttMove);
        if (!info.stopped.load(std::memory_order_relaxed)) {
            if (singularScore < singularBeta) {
                singularExt = 1;
                // Double extension: jesli TT move jest DUŻO lepszy od alternatyw
                if (singularScore < singularBeta - depth * 4)
                    singularExt = 2;
            } else if (singularScore >= beta) {
                return singularScore; // Multi-cut
            }
        }
    }

    // === Generuj i przeszukuj ruchy ===
    MoveList list;
    generate_moves(pos, list);
    // Countermove: jaki ruch obalil poprzedni ruch przeciwnika?
    Move counterMove = MOVE_NONE;
    Piece prevPc = NO_PIECE;
    Square prevTo = SQ_NONE;
    if (prevMove != MOVE_NONE) {
        prevPc = pos.piece_on(move_to(prevMove));
        prevTo = move_to(prevMove);
        if (prevPc != NO_PIECE)
            counterMove = countermoves[prevPc][prevTo];
    }
    // === Lazy move picking ===
    // Swap TT move na pozycje 0 (bez scorowania reszty).
    // Jesli TT move da cutoff, oszczedzamy scorowanie 30+ ruchow.
    int moveScores[256];
    bool movesScored = false;

    // Faza 0: TT move na front
    if (ttMove != MOVE_NONE) {
        for (int j = 0; j < list.count; j++) {
            if (list.moves[j] == ttMove) {
                if (j > 0) std::swap(list.moves[0], list.moves[j]);
                break;
            }
        }
    }

    int legalMoves = 0;
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INFINITE;
    TTFlag ttFlag = TT_ALPHA;

    Move quietsTried[64];
    int quietCount = 0;

    // Capture history: pamietaj bicia do penalty przy cutoff
    struct CapInfo { Piece pc; Square to; PieceType captured; };
    CapInfo capsTried[64];
    int capsCount = 0;

    int futMargin = params.futilityMargin * depth + (improving ? 50 : 0);
    bool canFutility = !inCheck && !pvNode && depth <= 3
                       && eval + futMargin < alpha;

    for (int i = 0; i < list.count; i++) {
        // Faza 1: ruch 0 = TT move (juz na froncie, bez scorowania)
        // Faza 2: ruch 1+ = scoruj reszte i pick best (lazy)
        if (i > 0) {
            if (!movesScored) {
                // Scoruj wszystkie ruchy od pozycji 1 (TT juz obsluzone)
                int start = (ttMove != MOVE_NONE) ? 1 : 0;
                for (int j = start; j < list.count; j++) {
                    moveScores[j] = move_score(pos, list.moves[j], MOVE_NONE, ply,
                                               counterMove, prevPc, prevTo);
                }
                movesScored = true;
            }
            // Selection sort: znajdz najlepszy od pozycji i
            int best = i;
            for (int j = i + 1; j < list.count; j++) {
                if (moveScores[j] > moveScores[best]) best = j;
            }
            if (best != i) {
                std::swap(list.moves[i], list.moves[best]);
                std::swap(moveScores[i], moveScores[best]);
            }
        }

        Move m = list.moves[i];

        if (m == excludedMove)
            continue;

        if (!pos.is_legal(m))
            continue;

        legalMoves++;
        bool isCapture = pos.piece_on(move_to(m)) != NO_PIECE || move_type(m) == EN_PASSANT;
        bool isPromo   = move_type(m) == PROMOTION;
        bool isQuiet   = !isCapture && !isPromo;

        // === Late Move Pruning (LMP) ===
        // LMP: wiecej ruchow sprawdzamy gdy pozycja sie poprawia
        int lmpThreshold = params.lmpBase + depth * depth + (improving ? depth : 0);
        if (!pvNode && !inCheck && isQuiet && depth <= 5 && legalMoves > lmpThreshold)
            continue;

        // === History Pruning ===
        // Prune ciche ruchy z bardzo negatywna historia na malych depth
        if (!pvNode && !inCheck && isQuiet && depth <= 3 && legalMoves > 1) {
            int hist = history[pos.side_to_move()][move_from(m)][move_to(m)];
            int cmHist = get_cm_history(prevPc, prevTo, m);
            if (hist + cmHist < -1000 * depth)
                continue;
        }

        // === Futility Pruning ===
        if (canFutility && legalMoves > 1 && isQuiet)
            continue;

        // Zapamietaj PRZED do_move (uzywane w SEE pruning, LMR, extensions)
        Piece capPiece = pos.piece_on(move_to(m));
        Piece movPiece = pos.piece_on(move_from(m));
        Color us = pos.side_to_move();

        // SEE Pruning: odrzuc bicia z negatywnym SEE
        // Pomijaj SEE dla oczywiscie dobrych bic (ofiara >= atakujacy)
        if (!pvNode && !inCheck && depth <= 3 && isCapture && legalMoves > 1) {
            if (PieceValue[type_of(movPiece)] > PieceValue[type_of(capPiece)] + 50) {
                int seeThreshold = (depth <= 2) ? -50 : 0;
                if (pos.see(m) < seeThreshold)
                    continue;
            }
        }

        // Zapisz cichy ruch do history penalty
        if (isQuiet && quietCount < 64)
            quietsTried[quietCount++] = m;

        // Zapisz bicie do capture history penalty
        if (isCapture && capsCount < 64)
            capsTried[capsCount++] = {movPiece, move_to(m), type_of(capPiece)};

        StateInfo newSt;
        pos.do_move(m, newSt);

        bool givesCheck = pos.in_check();
        int score;

        // Extensions: givesCheck + singular + recapture
        int ext = 0;
        // Check extension: TT move lub jedyny ruch
        // Nie rozszerzaj kazdego szacha — tylko wazne (TT, jedyny, lub early move)
        if (givesCheck && ply < 80) {
            if (legalMoves == 1 || m == ttMove)
                ext = 1;
            else if (legalMoves <= 3 && !isCapture)
                ext = 1; // wczesny cichy szach — warto zbadac
        }
        // Singular extension
        if (m == ttMove && singularExt > 0 && ext == 0)
            ext = singularExt;
        // Recapture extension: bijemy na polu gdzie przeciwnik wlasnie bil
        // Tylko gdy bita figura jest warta >= skoczka (unikamy pion-za-pion explosion)
        if (ext == 0 && isCapture && prevMove != MOVE_NONE
            && move_to(m) == move_to(prevMove) && ply < 80
            && capPiece != NO_PIECE && PieceValue[type_of(capPiece)] >= 300)
            ext = 1;

        // === PVS + LMR ===
        if (legalMoves == 1) {
            score = -alpha_beta(pos, depth - 1 + ext, -beta, -alpha, info, ply + 1, true, m);
        } else {
            // === LMR ===
            int reduction = 0;
            if (legalMoves > 3 && depth >= 3 && !inCheck && !givesCheck) {
                if (isQuiet) {
                    // Uzyj tabeli LMR (log-based)
                    reduction = lmrTable[std::min(depth, 63)][std::min(legalMoves, 63)];
                    if (!pvNode) reduction++;
                    if (!improving) reduction++; // pozycja sie pogarsza — szukaj szerzej
                    // Eval-based: jesli eval daleko pod alpha, redukuj wiecej
                    if (eval + 100 < alpha) reduction++;
                    // Eval-based: jesli eval wysoko nad alpha, redukuj mniej
                    if (eval > alpha + 100) reduction--;
                    // Mniejsza redukcja dla killer/countermove
                    if (ply < 128 && (m == killers[ply][0] || m == killers[ply][1]))
                        reduction--;
                    if (m == counterMove)
                        reduction--;
                    // History korekta — ciagle skalowanie zamiast progowego
                    int hist = history[us][move_from(m)][move_to(m)];
                    reduction -= std::max(-2, std::min(2, hist / 5000));
                } else if (isCapture) {
                    // LMR dla bic z negatywnym SEE
                    if (capPiece != NO_PIECE && movPiece != NO_PIECE) {
                        int capV = PieceValue[type_of(capPiece)];
                        int movV = PieceValue[type_of(movPiece)];
                        if (movV > capV + 100)
                            reduction = 1; // lekka redukcja prawdopodobnie zlego bicia
                    }
                }
                reduction = std::max(0, std::min(reduction, depth - 2));
            }

            // Zero-window search (PVS)
            score = -alpha_beta(pos, depth - 1 + ext - reduction, -alpha - 1, -alpha,
                                 info, ply + 1, true, m);

            if (score > alpha && reduction > 0) {
                score = -alpha_beta(pos, depth - 1 + ext, -alpha - 1, -alpha,
                                     info, ply + 1, true, m);
            }

            if (score > alpha && score < beta) {
                score = -alpha_beta(pos, depth - 1 + ext, -beta, -alpha, info, ply + 1, true, m);
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
                // Aktualizuj countermove history
                update_cm_history(prevPc, prevTo, m, depth * depth);
                if (prevPc != NO_PIECE)
                    countermoves[prevPc][prevTo] = m;
                // History penalty + cm history penalty dla ruchow ktore nie daly cutoff
                Color sideToMove = pos.side_to_move();
                for (int qi = 0; qi < quietCount - 1; qi++) {
                    Move qm = quietsTried[qi];
                    history[sideToMove][move_from(qm)][move_to(qm)] -= depth * depth;
                    if (history[sideToMove][move_from(qm)][move_to(qm)] < -1000000) {
                        for (int ff = 0; ff < 64; ff++)
                            for (int tt = 0; tt < 64; tt++)
                                history[sideToMove][ff][tt] /= 2;
                    }
                    update_cm_history(prevPc, prevTo, qm, -(depth * depth));
                }
            } else if (isCapture) {
                // Capture history: bonus dla bicia ktore dalo cutoff
                update_capture_history(movPiece, move_to(m), type_of(capPiece), depth * depth);
                // Penalty dla bic ktore nie daly cutoff
                for (int ci = 0; ci < capsCount - 1; ci++) {
                    update_capture_history(capsTried[ci].pc, capsTried[ci].to,
                                           capsTried[ci].captured, -(depth * depth));
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
        return -10; // pat z contempt — spójne z remisem
    }

    // Zapisz do tablicy transpozycji
    TT.store(pos.key(), bestScore, depth, bestMove, ttFlag, eval);

    return bestScore;
}

void clear_search_tables() {
    std::memset(history, 0, sizeof(history));
    std::memset(killers, 0, sizeof(killers));
    std::memset(countermoves, 0, sizeof(countermoves));
    std::memset(cmHistory, 0, sizeof(cmHistory));
    std::memset(captureHistory, 0, sizeof(captureHistory));
}

// === Iterative Deepening — glowna petla ===

SearchResult search(Position& pos, SearchInfo& info) {
    SearchResult result;
    result.bestMove = MOVE_NONE;
    result.ponderMove = MOVE_NONE;
    result.score = -VALUE_INFINITE;

    info.startTime = std::chrono::steady_clock::now();
    info.nodes.store(0, std::memory_order_relaxed);
    info.stopped.store(false);

    static bool lmrInit = false;
    if (!lmrInit) { init_lmr(); lmrInit = true; }

    std::memset(killers, 0, sizeof(killers));
    std::memset(countermoves, 0, sizeof(countermoves));

    // Smart time management: sledzenie stabilnosci najlepszego ruchu
    Move prevBestMove = MOVE_NONE;
    int  prevScore = -VALUE_INFINITE;
    int  stabilityCount = 0;  // ile iteracji z rzedu ten sam bestMove
    // Mnoznik czasu: 1.0 = normalny, <1.0 = oszczedz, >1.0 = mysl dluzej
    double timeFactor = 1.0;

    for (int depth = 1; depth <= info.maxDepth; depth++) {
        info.depth = depth;

        // === Aspiration windows ===
        // Zamiast szukac z oknem (-INF, +INF), zawezamy okno wokol
        // wyniku z poprzedniej iteracji. Jezeli wynik miesci sie w oknie
        // — oszczedzamy czas. Jezeli nie — poszerzamy okno i szukamy ponownie.
        int alpha, beta;
        if (depth >= 4 && std::abs(result.score) < VALUE_MATE - 100) {
            alpha = result.score - params.aspirationWindow;
            beta  = result.score + params.aspirationWindow;
        } else {
            alpha = -VALUE_INFINITE;
            beta  =  VALUE_INFINITE;
        }

        int score;
        int aspDelta = params.aspirationWindow; // startowe okno (25)
        while (true) {
            score = alpha_beta(pos, depth, alpha, beta, info, 0, true);

            if (info.stopped.load(std::memory_order_relaxed))
                break;

            // Fail-low: wynik ponizej okna — poszerz w dol stopniowo
            if (score <= alpha) {
                alpha = std::max(-VALUE_INFINITE, score - aspDelta);
                aspDelta *= 2; // 25 → 50 → 100 → 200 → ...
                if (aspDelta > 500) alpha = -VALUE_INFINITE;
                continue;
            }
            // Fail-high: wynik powyzej okna — poszerz w gore stopniowo
            if (score >= beta) {
                beta = std::min(VALUE_INFINITE, score + aspDelta);
                aspDelta *= 2;
                if (aspDelta > 500) beta = VALUE_INFINITE;
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
        int64_t nodeCount = info.nodes.load(std::memory_order_relaxed);
        int64_t nps = (elapsed > 0) ? (nodeCount * 1000 / elapsed) : nodeCount;

        // Pobierz PV z TT — bezpiecznie, max depth ruchow
        std::string pvStr;
        StateInfo pvStates[64];
        Move pvMoves[64];
        int pvLen = 0;
        for (int i = 0; i < depth && i < 60; i++) {
            bool hit;
            TTEntry* e = TT.probe(pos.key(), hit);
            if (!hit || e->move == MOVE_NONE) break;
            Move pvMove = e->move;
            // Weryfikuj ze ruch jest sensowny
            Square from = move_from(pvMove);
            Square to = move_to(pvMove);
            if (from >= 64 || to >= 64) break;
            if (pos.piece_on(from) == NO_PIECE) break;
            if (!pos.is_legal(pvMove)) break;
            pvStr += pos.move_to_uci(pvMove) + " ";
            pvMoves[pvLen] = pvMove;
            pos.do_move(pvMove, pvStates[pvLen]);
            pvLen++;
        }
        // Zapisz ponder move (2. ruch w PV = przewidywany ruch przeciwnika)
        if (pvLen >= 2)
            result.ponderMove = pvMoves[1];

        for (int i = pvLen - 1; i >= 0; i--)
            pos.undo_move(pvMoves[i]);

        std::cout << "info depth " << depth
                  << " score cp " << score
                  << " nodes " << nodeCount
                  << " nps " << nps
                  << " time " << elapsed
                  << " pv " << pvStr
                  << std::endl;

        if (std::abs(score) > VALUE_MATE - 100)
            break;

        // === Smart time management ===
        if (info.timeLimit > 0 && depth >= 5) {
            // Stabilnosc: najlepszy ruch sie zmienil?
            if (result.bestMove == prevBestMove) {
                stabilityCount++;
                // Stabilny ruch — zmniejsz timeFactor stopniowo
                if (timeFactor > 1.0)
                    timeFactor = std::max(1.0, timeFactor * 0.9);
            } else {
                stabilityCount = 0;
                timeFactor = std::min(timeFactor + 0.3, 2.0); // addytywny, nie multiplikatywny
            }

            // Eval spadl — klopoty, mysl dluzej (ale ograniczony efekt)
            if (prevScore > -VALUE_MATE + 100) {
                int drop = prevScore - score;
                if (drop > 50)
                    timeFactor = std::min(timeFactor + 0.3, 2.0);
                else if (drop > 25)
                    timeFactor = std::min(timeFactor + 0.15, 2.0);
            }

            prevBestMove = result.bestMove;
            prevScore = score;

            // Easy move: ruch stabilny od 6+ iteracji i depth >= 8 — oszczedz czas
            if (stabilityCount >= 6 && depth >= 8 && elapsed > info.timeLimit * 0.35) {
                break;
            }

            // Early stop: ruch stabilny od 4+ iteracji i zuzylismy >50% czasu
            if (stabilityCount >= 4 && elapsed > info.timeLimit * 0.5) {
                break;
            }

            // Hard stop: zuzylismy timeFactor * baseTime
            if (elapsed > info.timeLimit * timeFactor) {
                break;
            }
        }
    }

    // Safety: jesli nie znalezlismy zadnego ruchu, wybierz pierwszy legalny
    if (result.bestMove == MOVE_NONE) {
        MoveList fallback;
        generate_moves(pos, fallback);
        for (int i = 0; i < fallback.count; i++) {
            if (pos.is_legal(fallback.moves[i])) {
                result.bestMove = fallback.moves[i];
                break;
            }
        }
    }

    return result;
}

// === Lazy SMP ===
// Wiele watkow szuka rownolegle na kopiach pozycji, wspoldzielac TT.
// Watek glowny (id=0) zwraca wynik. Helpery (id>0) tylko wypelniaja TT.
// Kazdy helper szuka z lekko inna glebokoscia zeby dywersyfikowac search.

SearchResult search_smp(Position& pos, SearchInfo& info, int numThreads) {
    if (numThreads <= 1)
        return search(pos, info);

    // Ogranicz do rozsadnej liczby
    if (numThreads > 8) numThreads = 8;

    SearchResult mainResult;
    mainResult.bestMove = MOVE_NONE;
    mainResult.ponderMove = MOVE_NONE;
    mainResult.score = -VALUE_INFINITE;

    std::vector<std::thread> helpers;

    // Helpery: kopie pozycji, wspoldzielony SearchInfo (nodes, stopped, timeLimit)
    for (int t = 1; t < numThreads; t++) {
        helpers.emplace_back([&info, &pos, t]() {
            // Kopia pozycji — kazdy watek ma wlasna
            Position helperPos(pos);

            // Helper SearchInfo — wspoldzieli stopped/timeLimit z glownym,
            // ale ma wlasny nodes counter (dodamy do glownego na koncu)
            SearchInfo helperInfo;
            helperInfo.maxDepth = info.maxDepth;
            helperInfo.timeLimit = info.timeLimit;
            helperInfo.startTime = info.startTime;
            helperInfo.stopped.store(false);
            helperInfo.nodes.store(0);

            // Szukaj z lekko inna glebokoscia (dywersyfikacja)
            // Helper nie wypisuje info — tylko wypelnia TT
            for (int depth = 1; depth <= helperInfo.maxDepth; depth++) {
                if (info.stopped.load(std::memory_order_relaxed))
                    break;

                // Dywersyfikacja: co drugi helper zaczyna od depth+1
                int adjDepth = depth + (t % 2);
                if (adjDepth > helperInfo.maxDepth) adjDepth = helperInfo.maxDepth;

                int alpha = -VALUE_INFINITE;
                int beta = VALUE_INFINITE;
                alpha_beta(helperPos, adjDepth, alpha, beta, helperInfo, 0, true);

                if (info.stopped.load(std::memory_order_relaxed))
                    break;

                // Sprawdz czas
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - helperInfo.startTime).count();
                if (helperInfo.timeLimit > 0 && elapsed >= helperInfo.timeLimit)
                    break;
            }

            // Dodaj nodes do glownego
            info.nodes.fetch_add(helperInfo.nodes.load(), std::memory_order_relaxed);
        });
    }

    // Watek glowny — normalny search
    mainResult = search(pos, info);

    // Zatrzymaj helpery
    info.stopped.store(true, std::memory_order_relaxed);
    for (auto& t : helpers)
        t.join();

    return mainResult;
}
