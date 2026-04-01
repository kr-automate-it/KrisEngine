#include "tt.h"
#include <cstring>

TranspositionTable TT;

void TranspositionTable::resize(int sizeMB) {
    delete[] table;
    numEntries = (uint64_t)sizeMB * 1024 * 1024 / sizeof(TTEntry);
    table = new TTEntry[numEntries];
    clear();
}

void TranspositionTable::clear() {
    std::memset(table, 0, numEntries * sizeof(TTEntry));
    generation = 0;
}

// Helper: pakuj dane wpisu do U64 (bez key)
static U64 pack_data(const TTEntry& e) {
    U64 d = 0;
    d |= (U64)(uint16_t)e.score;
    d |= (U64)(uint16_t)e.depth       << 16;
    d |= (U64)(uint16_t)e.staticEval  << 32;
    d |= (U64)(uint8_t)e.flag         << 48;
    d |= (U64)e.age                   << 56;
    return d;
}

TTEntry* TranspositionTable::probe(U64 key, bool& found) {
    TTEntry* entry = &table[key % numEntries];

    // Lockless: weryfikuj XOR key
    U64 storedKey = entry->key;
    U64 data = pack_data(*entry);

    if ((storedKey ^ data) == key && entry->flag != TT_NONE) {
        found = true;
        return entry;
    }

    found = false;
    return entry;
}

void TranspositionTable::store(U64 key, int score, int depth, Move move, TTFlag flag, int staticEval) {
    TTEntry* entry = &table[key % numEntries];

    if (entry->flag == TT_NONE
        || entry->age != generation
        || depth >= entry->depth) {
        entry->score     = (int16_t)score;
        entry->depth     = (int16_t)depth;
        entry->staticEval = (int16_t)staticEval;
        entry->move      = move;
        entry->flag      = flag;
        entry->age       = generation;
        // Lockless: zapisz key XOR data — jesli inny watek nadpisze czesc
        // danych, XOR nie zgodzi sie przy odczycie i wpis zostanie odrzucony
        entry->key       = key ^ pack_data(*entry);
    }
}
