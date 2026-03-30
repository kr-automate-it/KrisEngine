#include "zobrist.h"
#include <random>

namespace Zobrist {
    U64 psq[PIECE_NB][SQUARE_NB];
    U64 enpassant[8];
    U64 castling[16];
    U64 side;

    // Uzywamy stalego seeda zeby hash byl deterministyczny
    // (ten sam program = te same hashe = powtarzalne wyniki)
    void init() {
        std::mt19937_64 rng(1070372);

        for (int p = 0; p < PIECE_NB; p++)
            for (int s = 0; s < SQUARE_NB; s++)
                psq[p][s] = rng();

        for (int f = 0; f < 8; f++)
            enpassant[f] = rng();

        for (int cr = 0; cr < 16; cr++)
            castling[cr] = rng();

        side = rng();
    }
}
