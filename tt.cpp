#include "tt.h"
#include <cstring>

TranspositionTable TT;

void TranspositionTable::resize(int sizeMB) {
    delete[] table;
    // Ile wpisow zmiesci sie w sizeMB?
    numEntries = (uint64_t)sizeMB * 1024 * 1024 / sizeof(TTEntry);
    table = new TTEntry[numEntries];
    clear();
}

void TranspositionTable::clear() {
    std::memset(table, 0, numEntries * sizeof(TTEntry));
    generation = 0;
}

TTEntry* TranspositionTable::probe(U64 key, bool& found) {
    TTEntry* entry = &table[key % numEntries];

    if (entry->key == key && entry->flag != TT_NONE) {
        found = true;
        return entry;
    }

    found = false;
    return entry;
}

void TranspositionTable::store(U64 key, int score, int depth, Move move, TTFlag flag) {
    TTEntry* entry = &table[key % numEntries];

    // Strategia wymiany:
    // - zawsze nadpisuj jezeli:
    //   * wpis jest pusty
    //   * nowa glebokosc >= stara
    //   * wpis jest z innej generacji (starej gry)
    if (entry->flag == TT_NONE
        || entry->age != generation
        || depth >= entry->depth) {
        entry->key   = key;
        entry->score = (int16_t)score;
        entry->depth = (int16_t)depth;
        entry->move  = move;
        entry->flag  = flag;
        entry->age   = generation;
    }
}
