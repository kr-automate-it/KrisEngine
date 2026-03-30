#pragma once
#include "types.h"

// Tablice losowych liczb dla hashowania Zobrist
namespace Zobrist {
    extern U64 psq[PIECE_NB][SQUARE_NB];   // figura na polu
    extern U64 enpassant[8];                // kolumna en passant (a-h)
    extern U64 castling[16];                // prawa roszady (4 bity = 16 kombinacji)
    extern U64 side;                        // strona do ruchu

    void init();
}
