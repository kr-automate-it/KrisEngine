#pragma once
#include "types.h"

// Pawn hash table — cache ewaluacji struktury pionkow.
// Struktura pionkow zmienia sie rzadko (tylko ruch/bicie pionka),
// wiec mozna ja cache'owac oddzielnie i unikac powtornych obliczen.

struct PawnEntry {
    U64 key;
    int mg;
    int eg;
    U64 passedPawns[2]; // bitboard passed pawns per color (WHITE, BLACK)
};

class PawnTable {
public:
    PawnTable();
    ~PawnTable() { delete[] table; }

    PawnEntry* probe(U64 key, bool& found);
    void store(U64 key, int mg, int eg, U64 passedW = 0, U64 passedB = 0);
    void clear();

private:
    static constexpr int SIZE = 16384; // 16K wpisow, malo pamieci
    PawnEntry* table;
};

extern PawnTable pawnTable;
