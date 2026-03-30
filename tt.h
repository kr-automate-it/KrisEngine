#pragma once
#include "types.h"

// Typ zapisu w TT — informuje co oznacza score
enum TTFlag : uint8_t {
    TT_NONE  = 0,
    TT_EXACT = 1,  // Dokladna wartosc (alpha < score < beta)
    TT_ALPHA = 2,  // Gorna granica (score <= alpha, fail-low)
    TT_BETA  = 3   // Dolna granica (score >= beta, fail-high / cutoff)
};

// Jeden wpis w tablicy — 16 bajtow, kompaktowy
struct TTEntry {
    U64      key;     // Pelny hash do weryfikacji (kolizje!)
    int16_t  score;   // Wynik ewaluacji
    int16_t  depth;   // Glebokosc przeszukiwania
    Move     move;    // Najlepszy ruch znaleziony
    TTFlag   flag;    // Typ wpisu
    uint8_t  age;     // Generacja (do wymiany starych wpisow)
};

class TranspositionTable {
public:
    TranspositionTable() : table(nullptr), numEntries(0), generation(0) {}
    ~TranspositionTable() { delete[] table; }

    // Alokuj tablice o danym rozmiarze w MB
    void resize(int sizeMB);

    // Szukaj wpisu dla danego hasha
    TTEntry* probe(U64 key, bool& found);

    // Zapisz wpis
    void store(U64 key, int score, int depth, Move move, TTFlag flag);

    // Nowa gra — zwieksz generacje (stare wpisy beda nadpisywane pierwsze)
    void new_generation() { generation++; }

    void clear();

private:
    TTEntry* table;
    uint64_t numEntries;
    uint8_t  generation;
};

extern TranspositionTable TT;
