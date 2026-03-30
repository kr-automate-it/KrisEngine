#include "pawns.h"
#include <cstring>

PawnTable pawnTable;

PawnTable::PawnTable() {
    table = new PawnEntry[SIZE];
    clear();
}

void PawnTable::clear() {
    std::memset(table, 0, SIZE * sizeof(PawnEntry));
}

PawnEntry* PawnTable::probe(U64 key, bool& found) {
    PawnEntry* e = &table[key % SIZE];
    found = (e->key == key);
    return e;
}

void PawnTable::store(U64 key, int mg, int eg) {
    PawnEntry* e = &table[key % SIZE];
    e->key = key;
    e->mg = mg;
    e->eg = eg;
}
