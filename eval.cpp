#include "eval.h"
#include "pawns.h"

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

int evaluate(const Position& pos) {
    int mg = 0, eg = 0;
    int phase = 0;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;

        PieceType pt = type_of(p);
        Color c = color_of(p);
        int pst_sq = (c == WHITE) ? sq : flip(sq);

        int mgVal = PieceValuesMG[pt] + PST_MG[pt][pst_sq];
        int egVal = PieceValuesEG[pt] + PST_EG[pt][pst_sq];

        if (c == WHITE) { mg += mgVal; eg += egVal; }
        else            { mg -= mgVal; eg -= egVal; }

        // Oblicz faze
        switch (pt) {
            case KNIGHT: phase += KnightPhase; break;
            case BISHOP: phase += BishopPhase; break;
            case ROOK:   phase += RookPhase;   break;
            case QUEEN:  phase += QueenPhase;  break;
            default: break;
        }
    }

    // Bonus za pare goncow
    if (popcount(pos.pieces(WHITE, BISHOP)) >= 2) { mg += 30; eg += 50; }
    if (popcount(pos.pieces(BLACK, BISHOP)) >= 2) { mg -= 30; eg -= 50; }

    U64 occ = pos.pieces();

    // === Struktura pionkow (z pawn hash cache) ===
    bool pawnHit;
    PawnEntry* pe = pawnTable.probe(pos.pawn_key(), pawnHit);
    if (pawnHit) {
        mg += pe->mg;
        eg += pe->eg;
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
            int r = (c == WHITE) ? rank_of(s) : 7 - rank_of(s); // rank z perspektywy koloru

            // Maska: kolumny sasiednie i wlasna, od nastepnego rzedu do konca
            // Pionek wolny (passed) = brak pionkow przeciwnika mogacych go zablokowac
            U64 fileMask = FileA_BB << f;
            U64 adjFiles = (f > 0 ? FileA_BB << (f-1) : 0) | (f < 7 ? FileA_BB << (f+1) : 0);
            U64 frontMask;
            if (c == WHITE) {
                // Wszystkie pola od rzedu r+1 do 7
                frontMask = ~0ULL;
                if (r < 7) frontMask = ~((1ULL << ((r + 1) * 8)) - 1);
                else frontMask = 0;
            } else {
                // Wszystkie pola od rzedu 0 do r-1 (z perspektywy bialych: 7-r)
                int realRank = rank_of(s);
                if (realRank > 0) frontMask = (1ULL << (realRank * 8)) - 1;
                else frontMask = 0;
            }

            bool passed = !(theirPawns & (fileMask | adjFiles) & frontMask);
            if (passed) {
                static const int passedBonusMG[] = {0, 5, 10, 15, 30, 50, 80, 0};
                static const int passedBonusEG[] = {0, 10, 20, 35, 60, 100, 150, 0};
                pawnMG += sign * passedBonusMG[r];
                pawnEG += sign * passedBonusEG[r];
            }

            bool isolated = !(ourPawns & adjFiles);
            if (isolated) {
                pawnMG -= sign * 10;
                pawnEG -= sign * 15;
            }

            if (popcount(ourPawns & fileMask) > 1) {
                pawnMG -= sign * 5;
                pawnEG -= sign * 10;
            }
        }
    }
    pawnTable.store(pos.pawn_key(), pawnMG, pawnEG);
    mg += pawnMG;
    eg += pawnEG;
    } // end else (pawn hash miss)

    // === Bezpieczenstwo krola ===
    // Tarcza pionkowa: sprawdz czy pionki stoja przed krolem
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        Square ksq = pos.king_square(c);
        int kf = file_of(ksq);
        int kr = rank_of(ksq);

        // Tylko w midgame, i gdy krol jest po roszadzie (na skrzydlach)
        if (kf <= 2 || kf >= 5) {
            int shieldBonus = 0;
            int shieldRank = (c == WHITE) ? kr + 1 : kr - 1;

            if (shieldRank >= 0 && shieldRank < 8) {
                for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
                    Square pawnSq = make_square(f, shieldRank);
                    if (pos.piece_on(pawnSq) == make_piece(c, PAWN))
                        shieldBonus += 10;  // pionek na 1 rzad przed krolem
                    else {
                        // Sprawdz 2 rzedy przed
                        int rank2 = (c == WHITE) ? kr + 2 : kr - 2;
                        if (rank2 >= 0 && rank2 < 8) {
                            Square pawnSq2 = make_square(f, rank2);
                            if (pos.piece_on(pawnSq2) == make_piece(c, PAWN))
                                shieldBonus += 5; // dalej = mniejszy bonus
                        }
                    }
                }
            }
            mg += sign * shieldBonus;
        }

        // Kara za otwarte/pol-otwarte linie przy krolu — wieze i hetmany wroga moga wejsc
        Color them = ~c;
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
            U64 fileBB = FileA_BB << f;
            bool ourPawnOnFile   = pos.pieces(c, PAWN) & fileBB;
            bool theirPawnOnFile = pos.pieces(them, PAWN) & fileBB;

            if (!ourPawnOnFile && !theirPawnOnFile) {
                // Otwarta linia przy krolu
                mg -= sign * 15;
            } else if (!ourPawnOnFile) {
                // Pol-otwarta linia
                mg -= sign * 8;
            }
        }
    }

    // === Mobilnosc + Wieze otwarte ===
    for (Color c : {WHITE, BLACK}) {
        int sign = (c == WHITE) ? 1 : -1;
        int mob = 0;

        U64 knights = pos.pieces(c, KNIGHT);
        while (knights) {
            Square s = pop_lsb(knights);
            mob += popcount(KnightAttacks[s] & ~pos.pieces(c));
        }
        U64 bishops = pos.pieces(c, BISHOP);
        while (bishops) {
            Square s = pop_lsb(bishops);
            mob += popcount(bishop_attacks(s, occ) & ~pos.pieces(c));
        }
        U64 rooks = pos.pieces(c, ROOK);
        while (rooks) {
            Square s = pop_lsb(rooks);
            mob += popcount(rook_attacks(s, occ) & ~pos.pieces(c));
            U64 fileBB = FileA_BB << file_of(s);
            if (!(pos.pieces(PAWN) & fileBB))       { mg += sign * 20; eg += sign * 15; }
            else if (!(pos.pieces(c, PAWN) & fileBB)) { mg += sign * 10; eg += sign * 8; }
            U64 rank7 = (c == WHITE) ? Rank7_BB : Rank2_BB;
            if (square_bb(s) & rank7) { mg += sign * 20; eg += sign * 30; }
        }
        U64 queens = pos.pieces(c, QUEEN);
        while (queens) {
            Square s = pop_lsb(queens);
            mob += popcount(queen_attacks(s, occ) & ~pos.pieces(c));
        }

        mg += sign * mob * 3;
        eg += sign * mob * 2;
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
                    mg += sign * 25;
                    eg += sign * 15;
                } else if (supported) {
                    mg += sign * 12;
                    eg += sign * 8;
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

    // === Tempo bonus ===
    // Strona do ruchu ma lekka przewage (moze zagrozic)
    mg += (pos.side_to_move() == WHITE) ? 15 : -15;
    eg += (pos.side_to_move() == WHITE) ? 5 : -5;

    // Tapered eval
    if (phase > TotalPhase) phase = TotalPhase;
    int score = (mg * phase + eg * (TotalPhase - phase)) / TotalPhase;

    return (pos.side_to_move() == WHITE) ? score : -score;
}
