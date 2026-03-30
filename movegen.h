#pragma once
#include "position.h"

struct MoveList {
    Move moves[256];
    int  count = 0;

    void add(Move m) { moves[count++] = m; }
};

// Generuj wszystkie pseudo-legalne ruchy
void generate_moves(const Position& pos, MoveList& list);

// Generuj tylko bicia (do quiescence search)
void generate_captures(const Position& pos, MoveList& list);
