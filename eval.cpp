#include "eval.h"
#include "pawns.h"
#include "params.h"
#include <cstring>

// === Tapered Eval ===
// Dwa zestawy tablic: midgame (MG) i endgame (EG).
// Faza gry obliczana z ilosci materialu na planszy:
//   duzo figur = midgame, malo figur = endgame.
// Wynik = interpolacja: (mg * phase + eg * (256 - phase)) / 256
//
// Dlaczego to wazne? Bo w midgame krol powinien byc schowany w rogu,
// a w endgame powinien isc do centrum. Pionki tez maja rozna wartosc
// (w endgame daleko wysuniete pionki sa warte wiecej).

// Wartosci materialowe
constexpr int PawnValueMG   = 100;  constexpr int PawnValueEG   = 120;
constexpr int KnightValueMG = 320;  constexpr int KnightValueEG = 310;
constexpr int BishopValueMG = 330;  constexpr int BishopValueEG = 340;
constexpr int RookValueMG   = 500;  constexpr int RookValueEG   = 550;
constexpr int QueenValueMG  = 950;  constexpr int QueenValueEG  = 1000;

// Fazy figur (do obliczania phase)
constexpr int KnightPhase = 1;
constexpr int BishopPhase = 1;
constexpr int RookPhase   = 2;
constexpr int QueenPhase  = 4;
constexpr int TotalPhase  = KnightPhase * 4 + BishopPhase * 4 + RookPhase * 4 + QueenPhase * 2;

// Piece-Square Tables — wartosci z perspektywy bialych (index = square A1..H8)
// Tablice bazowane na PeSTO (Piece-Square Tables Only)

// --- PAWN ---
static const int PawnMG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    -6, -4,  1,  1,  1,  1, -4, -6,
    -6, -4,  1,  2,  2,  1, -4, -6,
    -6, -4,  2, 12, 12,  2, -4, -6,
    -6, -4,  5, 15, 15,  5, -4, -6,
    -6, -4,  2, 12, 12,  2, -4, -6,
    -6, -4,  1,  1,  1,  1, -4, -6,
     0,  0,  0,  0,  0,  0,  0,  0,
};
static const int PawnEG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    -10, -6, 10, 10, 10, 10, -6,-10,
    -7,  1, 18, 22, 22, 18,  1, -7,
    -4,  8, 22, 32, 32, 22,  8, -4,
    20, 28, 38, 48, 48, 38, 28, 20,
    52, 60, 65, 70, 70, 65, 60, 52,
    88, 90, 95, 95, 95, 95, 90, 88,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// --- KNIGHT ---
static const int KnightMG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};
static const int KnightEG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  5, 20, 25, 25, 20,  5,-30,
    -30,  5, 20, 25, 25, 20,  5,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50,
};

// --- BISHOP ---
static const int BishopMG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};
static const int BishopEG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -10,  0, 10, 15, 15, 10,  0,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20,
};

// --- ROOK ---
static const int RookMG[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};
static const int RookEG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// --- QUEEN ---
static const int QueenMG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};
static const int QueenEG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5, 10, 10,  5,  0, -5,
     -5,  0,  5, 10, 10,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20,
};

// --- KING ---
static const int KingMG[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
};
// W endgame krol powinien isc do centrum!
static const int KingEG[64] = {
    -50,-30,-30,-30,-30,-30,-30,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50,
};

static const int* PST_MG[PIECE_TYPE_NB] = {
    nullptr, PawnMG, KnightMG, BishopMG, RookMG, QueenMG, KingMG
};
static const int* PST_EG[PIECE_TYPE_NB] = {
    nullptr, PawnEG, KnightEG, BishopEG, RookEG, QueenEG, KingEG
};

static const int PieceValuesMG[PIECE_TYPE_NB] = {
    0, PawnValueMG, KnightValueMG, BishopValueMG, RookValueMG, QueenValueMG, 0
};
static const int PieceValuesEG[PIECE_TYPE_NB] = {
    0, PawnValueEG, KnightValueEG, BishopValueEG, RookValueEG, QueenValueEG, 0
};

static int flip(int sq) { return sq ^ 56; }

// === Precomputed PSQ tables: Material + PST combined, per Piece (not PieceType) ===
// PSQ_MG[piece][sq] = materialMG + PST_MG for that piece on that square
// White pieces: sq used as-is. Black pieces: sq flipped.
static int psq_mg_data[PIECE_NB][SQUARE_NB];
static int psq_eg_data[PIECE_NB][SQUARE_NB];
const int (*PSQ_MG)[SQUARE_NB] = psq_mg_data;
const int (*PSQ_EG)[SQUARE_NB] = psq_eg_data;

static bool psq_initialized = false;
void init_psq_tables() {
    if (psq_initialized) return;
    std::memset(psq_mg_data, 0, sizeof(psq_mg_data));
    std::memset(psq_eg_data, 0, sizeof(psq_eg_data));
    for (int pt = PAWN; pt <= KING; pt++) {
        for (int sq = 0; sq < 64; sq++) {
            int wPiece = make_piece(WHITE, PieceType(pt));
            int bPiece = make_piece(BLACK, PieceType(pt));
            psq_mg_data[wPiece][sq] = PieceValuesMG[pt] + PST_MG[pt][sq];
            psq_eg_data[wPiece][sq] = PieceValuesEG[pt] + PST_EG[pt][sq];
            psq_mg_data[bPiece][sq] = PieceValuesMG[pt] + PST_MG[pt][flip(sq)];
            psq_eg_data[bPiece][sq] = PieceValuesEG[pt] + PST_EG[pt][flip(sq)];
        }
    }
    psq_initialized = true;
}

int evaluate(const Position& pos) {
    // Incremental material + PST (z StateInfo, aktualizowane w do_move)
    int mg = pos.psq_mg();
    int eg = pos.psq_eg();
    int phase = pos.game_phase();

    // #8: Cache king squares i piece counts na poczatku
    Square kingWh = pos.king_square(WHITE);
    Square kingBl = pos.king_square(BLACK);
    int numBishops[COLOR_NB] = {
        popcount(pos.pieces(WHITE, BISHOP)),
        popcount(pos.pieces(BLACK, BISHOP))
    };
    int numKnights[COLOR_NB] = {
        popcount(pos.pieces(WHITE, KNIGHT)),
        popcount(pos.pieces(BLACK, KNIGHT))
    };

    // #7: Bishop pair — uzyj cached counts
    if (numBishops[WHITE] >= 2) { mg += params.bishopPairMG; eg += params.bishopPairEG; }
    if (numBishops[BLACK] >= 2) { mg -= params.bishopPairMG; eg -= params.bishopPairEG; }

    U64 occ = pos.pieces();

    // === Struktura pionkow (z pawn hash cache) ===
    // Passed pawns bitboard — uzywany ponizej w bonusach zaleznych od figur
    U64 passedBB[COLOR_NB] = {0, 0};

    bool pawnHit;
    PawnEntry* pe = pawnTable.probe(pos.pawn_key(), pawnHit);
    if (pawnHit) {
        mg += pe->mg;
        eg += pe->eg;
        passedBB[WHITE] = pe->passedPawns[WHITE];
        passedBB[BLACK] = pe->passedPawns[BLACK];
    } else {
    int pawnMG = 0, pawnEG = 0;
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        U64 ourPawns   = pos.pieces(c, PAWN);
        U64 theirPawns = pos.pieces(them, PAWN);

        U64 pawns = ourPawns;
        while (pawns) {
            Square s = pop_lsb(pawns);
            int f = file_of(s);
            int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);

            U64 fileMask = FileA_BB << f;
            U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);
            U64 frontMask;
            if (c == WHITE) {
                frontMask = (r < 7) ? ~((1ULL << ((r + 1) * 8)) - 1) : 0;
            } else {
                int realRank = rank_of(s);
                frontMask = (realRank > 0) ? (1ULL << (realRank * 8)) - 1 : 0;
            }

            bool passed = !(theirPawns & (fileMask | adjFiles) & frontMask);
            if (passed) {
                passedBB[c] |= square_bb(s); // zapisz do bitboard
                static const int passedBonusMG[] = {0, 5, 10, 20, 35, 60, 90, 0};
                static const int passedBonusEG[] = {0, 10, 20, 40, 80, 130, 200, 0};
                pawnMG += sign * passedBonusMG[r] * params.passedPawnScale / 100;
                pawnEG += sign * passedBonusEG[r] * params.passedPawnScale / 100;
            }

            bool isolated = !(ourPawns & adjFiles);
            if (isolated) {
                pawnMG -= sign * params.isolatedPawnMG;
                pawnEG -= sign * params.isolatedPawnEG;
            }

            if (popcount(ourPawns & fileMask) > 1) {
                pawnMG -= sign * params.doubledPawnMG;
                pawnEG -= sign * params.doubledPawnEG;
            }

            // Backward pawn
            if (!isolated && r >= 2 && r <= 5) {
                U64 behindMask;
                if (c == WHITE) behindMask = (1ULL << ((r + 1) * 8)) - 1;
                else { int realRank = rank_of(s); behindMask = ~((1ULL << (realRank * 8)) - 1); }
                if (!(ourPawns & adjFiles & behindMask)) {
                    Square stopSq = (c == WHITE) ? Square(s + 8) : Square(s - 8);
                    if (stopSq >= SQ_A1 && stopSq <= SQ_H8) {
                        U64 theirPawnAtt = (c == WHITE)
                            ? ((theirPawns >> 9) & ~FileH_BB) | ((theirPawns >> 7) & ~FileA_BB)
                            : ((theirPawns << 7) & ~FileH_BB) | ((theirPawns << 9) & ~FileA_BB);
                        if (theirPawnAtt & square_bb(stopSq)) { pawnMG -= sign * 8; pawnEG -= sign * 12; }
                    }
                }
            }
        }
    }
    pawnTable.store(pos.pawn_key(), pawnMG, pawnEG, passedBB[WHITE], passedBB[BLACK]);
    mg += pawnMG;
    eg += pawnEG;
    } // end else (pawn hash miss)

    // === Passed pawn bonusy zalezne od figur (uzywa cached passedBB) ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        Square ourKing   = (c == WHITE) ? kingWh : kingBl;
        Square theirKing = (c == WHITE) ? kingBl : kingWh;

        U64 passed = passedBB[c];
        while (passed) {
            Square s = pop_lsb(passed);
            int f = file_of(s);
            int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            if (r < 2) continue;

            U64 fileMask = FileA_BB << f;
            U64 frontMask;
            if (c == WHITE) frontMask = (r < 7) ? ~((1ULL << ((r + 1) * 8)) - 1) : 0;
            else { int rr = rank_of(s); frontMask = (rr > 0) ? (1ULL << (rr * 8)) - 1 : 0; }

            U64 pathToPromo = fileMask & frontMask;
            bool freePath = !(pos.pieces() & pathToPromo);

            // Aggressive free path bonus — rośnie eksponencjalnie z rankiem
            if (freePath) {
                static const int freePathBonus[] = {0, 0, 10, 25, 50, 100, 200, 350};
                eg += sign * freePathBonus[r];
            }

            // Blocked
            Square stopSq = (c == WHITE) ? Square(s + 8) : Square(s - 8);
            if (stopSq >= SQ_A1 && stopSq <= SQ_H8 && pos.piece_on(stopSq) != NO_PIECE) {
                static const int blockedPenalty[] = {0, 0, 3, 8, 15, 30, 50, 0};
                eg -= sign * blockedPenalty[r];
            }

            // Rook behind passed pawn (obie strony)
            U64 behindMask = fileMask & ~frontMask & ~square_bb(s);
            if (pos.pieces(c, ROOK) & behindMask) { mg += sign * 10; eg += sign * 30; }
            if (pos.pieces(them, ROOK) & behindMask) { mg -= sign * 10; eg -= sign * 30; }

            // Rook in front of enemy passed pawn — blokuje
            if (pos.pieces(them, ROOK) & pathToPromo) { eg += sign * 15; }

            // King proximity do passed pawn (nie do pola promocji — do samego pionka)
            Square promoSq = (c == WHITE) ? make_square(f, 7) : make_square(f, 0);
            int ourDistPawn  = std::max(std::abs(file_of(ourKing) - f), std::abs(rank_of(ourKing) - rank_of(s)));
            int theirDistPawn = std::max(std::abs(file_of(theirKing) - f), std::abs(rank_of(theirKing) - rank_of(s)));
            // Bonus za bliskosc naszego krola, kara za bliskosc wrogiego
            eg += sign * (theirDistPawn - ourDistPawn) * 5;

            // King proximity do pola promocji (oddzielnie, wazniejsze na wysokich rankach)
            int ourDistPromo  = std::max(std::abs(file_of(ourKing) - f), std::abs(rank_of(ourKing) - rank_of(promoSq)));
            int theirDistPromo = std::max(std::abs(file_of(theirKing) - f), std::abs(rank_of(theirKing) - rank_of(promoSq)));
            eg += sign * (theirDistPromo - ourDistPromo) * (r >= 5 ? 12 : 6);

            // Rule of the square
            if (freePath) {
                int ranksToPromo = 7 - r;
                int kingToPromo = std::max(std::abs(file_of(theirKing) - f),
                                           std::abs(rank_of(theirKing) - rank_of(promoSq)));
                int effectiveKingDist = (pos.side_to_move() == c) ? kingToPromo : kingToPromo - 1;
                U64 theirHeavy = pos.pieces(them, ROOK) | pos.pieces(them, QUEEN);
                if (effectiveKingDist > ranksToPromo && !theirHeavy)
                    eg += sign * 350;
            }

            // Connected passed pawns — dwa passed obok siebie = ogromna grozba
            U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);
            if (passedBB[c] & adjFiles) {
                static const int connectedBonus[] = {0, 0, 5, 15, 30, 60, 120, 0};
                eg += sign * connectedBonus[r];
            }
        }
    }

    // === Rook cutting off king ===
    // Wieza na linii odcinajaca wrogiego krola od passed pawn = ogromna przewaga
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        if (!passedBB[c]) continue;
        U64 rooks = pos.pieces(c, ROOK);
        Square theirKsq = (them == WHITE) ? kingWh : kingBl;
        int tkr = rank_of(theirKsq);
        while (rooks) {
            Square rs = pop_lsb(rooks);
            int rr = rank_of(rs);
            // Wieza na rzedzie miedzy passed pawn a wrogim krolem
            // = krol odciety, nie moze pomoc w obronie
            if (c == WHITE && rr > tkr && rr < 7) {
                eg += sign * 20;
            } else if (c == BLACK && rr < tkr && rr > 0) {
                eg += sign * 20;
            }
        }
    }

    // Pawn-based attack units per color — wypelniane w king safety, uzywane w mobilnosci
    int pawnAttackUnits[COLOR_NB] = {0, 0};

    // === Bezpieczenstwo krola (attack units system) ===
    // Tarcza pionkowa + otwarte linie + pawn storm
    // Zebrane attack units przeliczane na kare z nieliniowej tablicy.

    // Nieliniowa tablica kar: index = attack units (0..99), wartosc = kara w cp
    // Wiecej atakujacych = eksponencjalnie gorsza pozycja krola
    static const int SafetyTable[100] = {
        0,  0,  1,  2,  3,  5,  7, 10, 13, 16,
       20, 24, 29, 34, 39, 45, 51, 58, 65, 72,
       80, 88, 97,106,115,125,135,146,157,168,
      180,192,205,218,231,245,259,274,289,304,
      320,336,353,370,387,405,423,442,461,480,
      500,500,500,500,500,500,500,500,500,500,
      500,500,500,500,500,500,500,500,500,500,
      500,500,500,500,500,500,500,500,500,500,
      500,500,500,500,500,500,500,500,500,500,
      500,500,500,500,500,500,500,500,500,500,
    };

    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        Square ksq = (c == WHITE) ? kingWh : kingBl;
        int kf = file_of(ksq);
        int kr = rank_of(ksq);

        int attackUnits = 0;

        // --- Tarcza pionkowa ---
        // Sprawdzamy 3 kolumny wokol krola, 2 rzedu do przodu
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
            int shieldRank1 = (c == WHITE) ? kr + 1 : kr - 1;
            int shieldRank2 = (c == WHITE) ? kr + 2 : kr - 2;
            bool found = false;

            if (shieldRank1 >= 0 && shieldRank1 < 8) {
                Square sq1 = make_square(f, shieldRank1);
                if (pos.piece_on(sq1) == make_piece(c, PAWN)) {
                    // Pionek tuż przed królem — idealnie
                    found = true;
                }
            }
            if (!found && shieldRank2 >= 0 && shieldRank2 < 8) {
                Square sq2 = make_square(f, shieldRank2);
                if (pos.piece_on(sq2) == make_piece(c, PAWN)) {
                    attackUnits += 1; // pionek 2 rzedu dalej — slabsza tarcza
                    found = true;
                }
            }
            if (!found) {
                // Brak pionka na kolumnie tarczy — luka
                attackUnits += 3;
            }
        }

        // --- Otwarte/pol-otwarte linie przy krolu ---
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
            U64 fileBB = FileA_BB << f;
            bool ourPawnOnFile   = pos.pieces(c, PAWN) & fileBB;
            bool theirPawnOnFile = pos.pieces(them, PAWN) & fileBB;

            if (!ourPawnOnFile && !theirPawnOnFile) {
                attackUnits += 5; // otwarta linia = bardzo niebezpiecznie
            } else if (!ourPawnOnFile) {
                attackUnits += 3; // pol-otwarta na nas
            }
        }

        // --- Pawn storm: wrogi pionek blisko naszego krola ---
        U64 theirPawns = pos.pieces(them, PAWN);
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
            U64 fileBB = FileA_BB << f;
            U64 stormPawns = theirPawns & fileBB;
            while (stormPawns) {
                Square sp = pop_lsb(stormPawns);
                int stormRank = (c == WHITE) ? rank_of(sp) : 7 - rank_of(sp);
                // Im blizej naszego krola, tym gorzej (rank z naszej perspektywy)
                if (stormRank >= 4) attackUnits += stormRank - 3; // +1 za rank5, +2 za rank6, +3 za rank7
            }
        }

        pawnAttackUnits[c] = attackUnits;
    }

    // === Mobilnosc + King Attack + Threat Detection ===
    // Zbieramy ataki kazdego koloru w jednej petli.
    // Po petli: threat detection na podstawie zebranych atakow.
    U64 allAttacks[COLOR_NB] = {0, 0};
    U64 pawnAtt[COLOR_NB] = {0, 0};
    U64 minorAtt[COLOR_NB] = {0, 0};  // skoczki + gonce
    U64 rookAtt[COLOR_NB] = {0, 0};

    // Ataki pionkow (tanie — z tablicy)
    for (Color c : {WHITE, BLACK}) {
        U64 pawns = pos.pieces(c, PAWN);
        if (c == WHITE)
            pawnAtt[c] = ((pawns << 7) & ~FileH_BB) | ((pawns << 9) & ~FileA_BB);
        else
            pawnAtt[c] = ((pawns >> 9) & ~FileH_BB) | ((pawns >> 7) & ~FileA_BB);
        allAttacks[c] |= pawnAtt[c];
    }

    // Attack weights per piece type (w attack units):
    // Skoczek/goniec=2, wieza=3, hetman=5
    static const int AttackWeight[PIECE_TYPE_NB] = {0, 0, 2, 2, 3, 5, 0};

    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        int mobMG = 0, mobEG = 0;
        int attackerCount = 0;  // ile figur atakuje krolowska strefe
        int attackUnits = pawnAttackUnits[them]; // zaczynamy od pawn-based units PRZECIWNIKA
        Square theirKsq = (them == WHITE) ? kingWh : kingBl;
        U64 kingZone = KingAttacks[theirKsq] | square_bb(theirKsq); // strefa krola + sam krol

        // Safe mobility: nie licz pol atakowanych przez wrogi pionek
        U64 safeMask = ~pos.pieces(c) & ~pawnAtt[them];

        // King tropism: bonus za figury blisko wrogiego krola (Chebyshev distance)
        int tkf = file_of(theirKsq), tkr = rank_of(theirKsq);
        int tropismMG = 0;

        U64 knights = pos.pieces(c, KNIGHT);
        while (knights) {
            Square s = pop_lsb(knights);
            U64 att = KnightAttacks[s];
            int cnt = popcount(att & safeMask);
            mobMG += cnt * params.knightMobMG; mobEG += cnt * params.knightMobEG;
            // King tropism: skoczek blisko krola = grozba
            int dist = std::max(std::abs(file_of(s) - tkf), std::abs(rank_of(s) - tkr));
            tropismMG += (7 - dist) * 3; // 0-21
            if (att & kingZone) {
                attackerCount++;
                attackUnits += AttackWeight[KNIGHT];
                if (att & square_bb(theirKsq))
                    attackUnits += 1;
            }
            allAttacks[c] |= att;
            minorAtt[c] |= att;
        }
        U64 bishops = pos.pieces(c, BISHOP);
        while (bishops) {
            Square s = pop_lsb(bishops);
            U64 att = bishop_attacks(s, occ);
            int cnt = popcount(att & safeMask);
            mobMG += cnt * params.bishopMobMG; mobEG += cnt * params.bishopMobEG;
            int dist = std::max(std::abs(file_of(s) - tkf), std::abs(rank_of(s) - tkr));
            tropismMG += (7 - dist) * 2; // 0-14
            if (att & kingZone) {
                attackerCount++;
                attackUnits += AttackWeight[BISHOP];
            }
            allAttacks[c] |= att;
            minorAtt[c] |= att;
        }
        U64 rooks = pos.pieces(c, ROOK);
        while (rooks) {
            Square s = pop_lsb(rooks);
            U64 att = rook_attacks(s, occ);
            int cnt = popcount(att & safeMask);
            mobMG += cnt * params.rookMobMG; mobEG += cnt * params.rookMobEG;
            int dist = std::max(std::abs(file_of(s) - tkf), std::abs(rank_of(s) - tkr));
            tropismMG += (7 - dist) * 3; // 0-21
            if (att & kingZone) {
                attackerCount++;
                attackUnits += AttackWeight[ROOK];
                int rf = file_of(s);
                int kf_them = file_of(theirKsq);
                if (std::abs(rf - kf_them) <= 1) {
                    U64 fileBB = FileA_BB << rf;
                    if (!(pos.pieces(PAWN) & fileBB))
                        attackUnits += 2;
                }
            }
            allAttacks[c] |= att;
            rookAtt[c] |= att;
            U64 fileBB = FileA_BB << file_of(s);
            if (!(pos.pieces(PAWN) & fileBB))         { mg += sign * params.rookOpenFileMG; eg += sign * params.rookOpenFileEG; }
            else if (!(pos.pieces(c, PAWN) & fileBB)) { mg += sign * (params.rookOpenFileMG/2); eg += sign * (params.rookOpenFileEG/2); }
            if (square_bb(s) & ((c == WHITE) ? Rank7_BB : Rank2_BB))
                { mg += sign * params.rook7thRankMG; eg += sign * params.rook7thRankEG; }
        }
        U64 queens = pos.pieces(c, QUEEN);
        while (queens) {
            Square s = pop_lsb(queens);
            U64 att = queen_attacks(s, occ);
            int cnt = popcount(att & ~pos.pieces(c));
            mobMG += cnt * params.queenMobMG; mobEG += cnt * params.queenMobEG;
            int dist = std::max(std::abs(file_of(s) - tkf), std::abs(rank_of(s) - tkr));
            tropismMG += (7 - dist) * 4; // 0-28
            if (att & kingZone) {
                attackerCount++;
                attackUnits += AttackWeight[QUEEN];
                if (att & square_bb(theirKsq))
                    attackUnits += 3;
            }
            allAttacks[c] |= att;
        }
        allAttacks[c] |= KingAttacks[(c == WHITE) ? kingWh : kingBl];

        mg += sign * mobMG;
        eg += sign * mobEG;

        // King tropism: bonus w midgame za figury blisko wrogiego krola
        mg += sign * tropismMG / 4;

        // === King safety — finalny wynik z attack units ===
        // Kara stosowana tylko gdy co najmniej 2 figury atakuja
        if (attackerCount >= 2) {
            // Skaluj attackUnits przez ilosc atakujacych figur
            attackUnits = attackUnits * (attackerCount - 1) / 2;
            if (attackUnits < 0) attackUnits = 0;
            if (attackUnits > 99) attackUnits = 99;
            int penalty = SafetyTable[attackUnits];
            // Kara dla PRZECIWNIKA (my atakujemy jego krola = dobrze dla nas)
            mg += sign * penalty;
            eg += sign * (penalty * 3 / 10); // 30% komponent w EG
        }
    }

    // === Threat Detection ===
    // Kara za figury atakowane przez tansze figury przeciwnika.
    // Np. pionek atakuje skoczka, skoczek atakuje wieze — to grozba.
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;

        // 1. Nasze figury atakowane przez wrogi pionek (bardzo grozne)
        U64 threatenedByPawn = pos.pieces(c, KNIGHT, BISHOP) & pawnAtt[them];
        mg -= sign * popcount(threatenedByPawn) * 25;
        eg -= sign * popcount(threatenedByPawn) * 15;

        U64 majorByPawn = (pos.pieces(c, ROOK) | pos.pieces(c, QUEEN)) & pawnAtt[them];
        mg -= sign * popcount(majorByPawn) * 35;
        eg -= sign * popcount(majorByPawn) * 20;

        // 2. Nasze wieze/hetman atakowane przez wrogi skoczek/goniec
        U64 majorByMinor = (pos.pieces(c, ROOK) | pos.pieces(c, QUEEN)) & minorAtt[them];
        mg -= sign * popcount(majorByMinor) * 20;
        eg -= sign * popcount(majorByMinor) * 10;

        // 3. Nasz hetman atakowany przez wroga wieze
        U64 queenByRook = pos.pieces(c, QUEEN) & rookAtt[them];
        mg -= sign * popcount(queenByRook) * 20;
        eg -= sign * popcount(queenByRook) * 10;

        // 4. Hanging pieces: nasze figury atakowane i nie bronione
        U64 attacked = pos.pieces(c) & allAttacks[them];
        U64 defended = pos.pieces(c) & allAttacks[c];
        U64 hanging = attacked & ~defended & ~pos.pieces(c, PAWN); // nie liczymy pionkow
        mg -= sign * popcount(hanging) * 15;
        eg -= sign * popcount(hanging) * 20;
    }

    // === Knight outposts ===
    // Skoczek na silnym polu: w polowie planszy przeciwnika, broniony pionkiem,
    // nie do zaatakowania przez wrogi pionek. Klasyczny motyw pozycyjny.
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        U64 knights = pos.pieces(c, KNIGHT);
        while (knights) {
            Square s = pop_lsb(knights);
            int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s);
            int f = file_of(s);

            // Musi byc na polowie przeciwnika (rank >= 4 z perspektywy koloru)
            if (r >= 4) {
                // Broniony wlasnym pionkiem?
                bool supported = PawnAttacks[them][s] & pos.pieces(c, PAWN);
                // Czy wrogi pionek moze go zaatakowac?
                U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);
                U64 frontRanks;
                if (c == WHITE)
                    frontRanks = (r < 7) ? ~((1ULL << ((r+1)*8)) - 1) : 0;
                else {
                    int realRank = rank_of(s);
                    frontRanks = (realRank > 0) ? (1ULL << (realRank*8)) - 1 : 0;
                }
                bool cantBeAttacked = !(pos.pieces(them, PAWN) & adjFiles & frontRanks);

                if (supported && cantBeAttacked) {
                    mg += sign * params.knightOutpostMG;
                    eg += sign * params.knightOutpostEG;
                } else if (supported) {
                    mg += sign * (params.knightOutpostMG / 2);
                    eg += sign * (params.knightOutpostEG / 2);
                }
            }
        }
    }

    // === Connected pawns ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        U64 pawns = pos.pieces(c, PAWN);
        U64 connected = pawns & ((pawns << 1) & ~FileA_BB | (pawns >> 1) & ~FileH_BB);
        mg += sign * popcount(connected) * 5;
        eg += sign * popcount(connected) * 8;
    }

    // === Bad bishop ===
    // Goniec zablokowany wlasnymi pionkami na tym samym kolorze pol.
    // Im wiecej pionkow na kolorze gonca, tym goniec mniej uzyteczny.
    // Jasne pola: a2,b1,c2,d1... = (file+rank) parzyste
    // Ciemne pola: a1,b2,c1,d2... = (file+rank) nieparzyste
    constexpr U64 LightSquares = 0x55AA55AA55AA55AAULL; // pola jasne
    constexpr U64 DarkSquares  = 0xAA55AA55AA55AA55ULL; // pola ciemne
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        U64 bishops = pos.pieces(c, BISHOP);
        U64 ourPawns = pos.pieces(c, PAWN);
        while (bishops) {
            Square s = pop_lsb(bishops);
            // Na jakim kolorze stoi goniec?
            bool onLight = LightSquares & square_bb(s);
            U64 sameColorPawns = ourPawns & (onLight ? LightSquares : DarkSquares);
            int blocked = popcount(sameColorPawns);
            // Kara: 4cp MG, 6cp EG za kazdy pionek blokujacy gonca
            mg -= sign * blocked * 4;
            eg -= sign * blocked * 6;
        }
    }

    // === Connected rooks ===
    // Bonus za wieze na tym samym rzedzie bez figur miedzy nimi.
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        U64 rooks = pos.pieces(c, ROOK);
        if (popcount(rooks) >= 2) {
            // Wyciagnij obie wieze
            U64 tmp = rooks;
            Square r1 = pop_lsb(tmp);
            Square r2 = pop_lsb(tmp);
            // Na tym samym rzedzie?
            if (rank_of(r1) == rank_of(r2)) {
                // Sprawdz czy miedzy nimi nic nie stoi
                int minF = std::min(file_of(r1), file_of(r2));
                int maxF = std::max(file_of(r1), file_of(r2));
                U64 between = 0;
                for (int f = minF + 1; f < maxF; f++)
                    between |= square_bb(make_square(f, rank_of(r1)));
                if (!(occ & between)) {
                    mg += sign * 10;
                    eg += sign * 15;
                }
            }
            // Na tej samej kolumnie?
            else if (file_of(r1) == file_of(r2)) {
                int minR = std::min(rank_of(r1), rank_of(r2));
                int maxR = std::max(rank_of(r1), rank_of(r2));
                U64 between = 0;
                for (int r = minR + 1; r < maxR; r++)
                    between |= square_bb(make_square(file_of(r1), r));
                if (!(occ & between)) {
                    mg += sign * 8;
                    eg += sign * 12;
                }
            }
        }
    }

    // === Bishop vs Knight: open/closed position ===
    // W otwartych pozycjach (malo pionkow w centrum) goniec > skoczek.
    // W zamknietych (duzo pionkow) skoczek > goniec.
    {
        constexpr U64 centerMask = (FileC_BB | FileD_BB | FileE_BB | FileF_BB)
                                 & (Rank3_BB | Rank4_BB | Rank5_BB | Rank6_BB);
        int centralPawns = popcount(pos.pieces(PAWN) & centerMask);
        // centralPawns: 0-2 = otwarta, 3-4 = srednia, 5+ = zamknieta
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            int nB = numBishops[c];
            int nK = numKnights[c];
            if (nB > nK) {
                // Mamy wiecej goncow — bonus w otwartej, kara w zamknietej
                int openBonus = (4 - centralPawns) * 5; // otwarta: +20, zamknieta: -5
                mg += sign * openBonus;
                eg += sign * openBonus;
            } else if (nK > nB) {
                // Mamy wiecej skoczkow — odwrotnie
                int closedBonus = (centralPawns - 2) * 5; // zamknieta: +15, otwarta: -10
                mg += sign * closedBonus;
                eg += sign * closedBonus;
            }
        }
    }

    // === Central control ===
    // Bonus za figury atakujace centralne pola (d4/d5/e4/e5).
    // Kontrola centrum = wiecej przestrzeni i mozliwosci taktycznych.
    {
        constexpr U64 center4 = square_bb(SQ_D4) | square_bb(SQ_D5)
                               | square_bb(SQ_E4) | square_bb(SQ_E5);
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            int centerControl = popcount(allAttacks[c] & center4);
            mg += sign * centerControl * 5;  // 5cp za kazde kontrolowane pole centralne
            eg += sign * centerControl * 2;
        }
    }

    // === Space evaluation ===
    // Kontrola pol na polowie przeciwnika za wlasnym lancuchem pionkow.
    // Im wiecej bezpiecznych pol, tym wiecej manewrow dla figur.
    // Tylko w middlegame (duzo figur), centralne kolumny (C-F).
    if (phase >= TotalPhase / 2) {
        constexpr U64 centerFiles = FileC_BB | FileD_BB | FileE_BB | FileF_BB;
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            Color them = ~c;
            U64 ourPawns = pos.pieces(c, PAWN);
            // Pola za naszymi pionkami (bezpieczne dla figur)
            U64 behindPawns;
            if (c == WHITE) {
                behindPawns = (ourPawns >> 8) | (ourPawns >> 16) | (ourPawns >> 24);
                behindPawns &= Rank2_BB | Rank3_BB | Rank4_BB;
            } else {
                behindPawns = (ourPawns << 8) | (ourPawns << 16) | (ourPawns << 24);
                behindPawns &= Rank5_BB | Rank6_BB | Rank7_BB;
            }
            // Space = pola za pionkami, centralne kolumny, nie atakowane przez wrogi pionek
            U64 safe = behindPawns & centerFiles & ~pawnAtt[them];
            mg += sign * popcount(safe) * 2;
        }
    }

    // === Trade bonus (endgame) ===
    // Gdy masz przewage materialna — wymiana figur jest korzystna (latwiej wygrasz).
    // Gdy jestes gorzej — unikaj wymian (trudniej przegrac z wiekszym materialem).
    // Implementacja: skaluj eval w strone 0 gdy duzo figur na planszy i masz przewage,
    // lub od 0 gdy malo figur i masz przewage.
    // Prostsze podejscie: bonus za mniej figur na planszy gdy prowadzisz.
    {
        // Material z perspektywy bialych (mg score bez pozycyjnych)
        int whiteMat = 0, blackMat = 0;
        for (int pt = PAWN; pt <= QUEEN; pt++) {
            whiteMat += popcount(pos.pieces(WHITE, PieceType(pt))) * PieceValuesMG[pt];
            blackMat += popcount(pos.pieces(BLACK, PieceType(pt))) * PieceValuesMG[pt];
        }
        int matDiff = whiteMat - blackMat; // >0 = bialy prowadzi
        // Im mniej figur na planszy, tym wieksza przewaga strony prowadzacej
        // (bo mniej szans na komplikacje/kontratak)
        int totalMat = whiteMat + blackMat;
        // Skalowanie: pelen bonus w EG, brak w MG
        // trade bonus = matDiff * (1 - totalMat/maxMat) * scale
        // maxMat ~= 2*(8*100 + 2*320 + 2*330 + 2*500 + 950) = ~7800
        if (matDiff > 0) {
            int tradeBonus = matDiff * (7800 - totalMat) / 7800 / 10;
            eg += tradeBonus;
        } else if (matDiff < 0) {
            int tradeBonus = (-matDiff) * (7800 - totalMat) / 7800 / 10;
            eg -= tradeBonus;
        }
    }

    // === King centralization w endgame ===
    // Krol w centrum wygrywa koncowki — agresywny bonus.
    // Skalowany przez faze: pelny efekt w czystym EG, malejacy w MG.
    {
        // Ile jestesmy w endgame: 0 = full MG, TotalPhase = impossibru
        int egWeight = TotalPhase - phase; // 0..TotalPhase
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            Square ksq = (c == WHITE) ? kingWh : kingBl;
            int kf = file_of(ksq);
            int kr = rank_of(ksq);
            int distFromCenter = std::max(std::abs(kf - 3), std::abs(kr - 3)); // 0=centrum, 3=rog
            // Max bonus: 30cp w centrum, 0 na brzegu, -15 w rogu
            int bonus = (3 - distFromCenter) * 10;
            eg += sign * bonus * egWeight / TotalPhase;
        }
    }

    // === Pawn majority ===
    // Bonus za wiecej pionkow na jednym skrzydle — mogą stworzyc passed pawna.
    // Kingsight = kolumny E-H, queenside = kolumny A-D.
    {
        constexpr U64 kingSide = FileE_BB | FileF_BB | FileG_BB | FileH_BB;
        constexpr U64 queenSide = FileA_BB | FileB_BB | FileC_BB | FileD_BB;
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            Color them = ~c;
            int ourKS  = popcount(pos.pieces(c, PAWN) & kingSide);
            int ourQS  = popcount(pos.pieces(c, PAWN) & queenSide);
            int theirKS = popcount(pos.pieces(them, PAWN) & kingSide);
            int theirQS = popcount(pos.pieces(them, PAWN) & queenSide);
            // Bonus za wiekszosc na skrzydle (moze stworzyc passed pawna)
            if (ourKS > theirKS) eg += sign * (ourKS - theirKS) * 8;
            if (ourQS > theirQS) eg += sign * (ourQS - theirQS) * 8;
        }
    }

    // === Piece activity w endgame ===
    // Kara za figury na brzegu planszy (rank 1/8 lub file A/H) w koncowce.
    // Aktywne figury w centrum sa warte wiecej.
    if (phase < TotalPhase * 2 / 3) {
        constexpr U64 edgeSquares = FileA_BB | FileH_BB | Rank1_BB | Rank8_BB;
        for (Color c : {WHITE, BLACK}) {
            int sign = (c == WHITE) ? 1 : -1;
            // Skoczki i gonce na brzegu — kara
            U64 minors = pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP);
            int edgeMinors = popcount(minors & edgeSquares);
            eg -= sign * edgeMinors * 10;
        }
    }

    // === Tempo bonus ===
    mg += (pos.side_to_move() == WHITE) ? params.tempoMG : -params.tempoMG;
    eg += (pos.side_to_move() == WHITE) ? params.tempoEG : -params.tempoEG;

    // === Mating with major pieces (KQ vs K, KR vs K, KQR vs K) ===
    // Gdy mamy ogromna przewage materialna i wrog ma samego krola (lub prawie),
    // pchaj wrogiego krola na brzeg/rog i naszego krola blisko.
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Color them = ~c;
        // Sprawdz czy wrog ma tylko krola (lub krola + max 1 pionka)
        U64 theirPieces = pos.pieces(them) & ~pos.pieces(them, KING);
        int theirPieceCount = popcount(theirPieces);
        // Czy mamy wystarczajaco duzo materialu do matowania?
        bool hasQueen = pos.pieces(c, QUEEN) != 0;
        bool hasRook  = pos.pieces(c, ROOK) != 0;

        if (theirPieceCount <= 1 && (hasQueen || hasRook)) {
            Square ourKing   = (c == WHITE) ? kingWh : kingBl;
            Square theirKing = (c == WHITE) ? kingBl : kingWh;
            int tkf = file_of(theirKing);
            int tkr = rank_of(theirKing);
            // Dystans wroga krola od centrum — im dalej = lepiej dla nas
            int edgeDist = std::min(std::min(tkf, 7 - tkf), std::min(tkr, 7 - tkr)); // 0=brzeg, 3=centrum
            int cornerBonus = (3 - edgeDist) * 30; // 0-90 za pchanie na brzeg
            // Dystans miedzy krolami — blizej = lepiej (pomagamy matowac)
            int kingDist = std::max(std::abs(file_of(ourKing) - tkf),
                                     std::abs(rank_of(ourKing) - tkr));
            int closenessBonus = (7 - kingDist) * 10; // 0-70 za bliskosc
            eg += sign * (cornerBonus + closenessBonus);
        }
    }

    // Tapered eval
    if (phase > TotalPhase) phase = TotalPhase;
    int score = (mg * phase + eg * (TotalPhase - phase)) / TotalPhase;

    // === Scale factor ===
    // Skaluj eval w dol gdy strona prowadzaca ma male szanse na wygrana:
    // - opposite color bishops bez wierz/hetmanow
    // - brak pionkow po stronie prowadzacej
    int scaleFactor = 64; // domyslnie 64/64 = 100%
    {
        Color stronger = (score > 0) ? WHITE : BLACK;
        Color weaker   = ~stronger;
        int strongPawns = popcount(pos.pieces(stronger, PAWN));
        int strongMinors = popcount(pos.pieces(stronger, KNIGHT)) + popcount(pos.pieces(stronger, BISHOP));
        int strongMajors = popcount(pos.pieces(stronger, ROOK)) + popcount(pos.pieces(stronger, QUEEN));

        // Brak pionkow — trudno wygrać
        if (strongPawns == 0) {
            if (strongMajors == 0) {
                // Tylko lekkie figury — max 1 goniec lub skoczek nie wygrywa
                if (strongMinors <= 1)
                    scaleFactor = 0; // remis
                else if (strongMinors == 2)
                    scaleFactor = 16; // 2 lekkie bez pionkow — prawie remis
            } else {
                scaleFactor = 48; // ciezkie figury bez pionkow — mozliwe ale trudne
            }
        }

        // Opposite color bishops (bez wierz/hetmanow) — remisowosc
        if (strongMajors == 0 && popcount(pos.pieces(stronger, BISHOP)) == 1
            && popcount(pos.pieces(weaker, BISHOP)) == 1) {
            constexpr U64 LightSq = 0x55AA55AA55AA55AAULL;
            Square sb = lsb(pos.pieces(stronger, BISHOP));
            Square wb = lsb(pos.pieces(weaker, BISHOP));
            bool oppColor = ((LightSq & square_bb(sb)) != 0) != ((LightSq & square_bb(wb)) != 0);
            if (oppColor)
                scaleFactor = std::min(scaleFactor, strongPawns <= 1 ? 16 : 32);
        }
    }
    // Aplikuj scale factor do endgame czesci
    int egScaled = eg * scaleFactor / 64;
    score = (mg * phase + egScaled * (TotalPhase - phase)) / TotalPhase;

    // === Rule50 scaling ===
    // Im blizej 50-move rule, tym bardziej eval zbliża się do 0
    int rule50 = pos.halfmove_clock();
    if (rule50 > 0)
        score = score * (100 - rule50) / 100;

    return (pos.side_to_move() == WHITE) ? score : -score;
}
