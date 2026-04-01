#pragma once
#include "position.h"

constexpr int VALUE_INFINITE = 32000;
constexpr int VALUE_MATE     = 31000;
constexpr int VALUE_NONE     = 32001;
constexpr int VALUE_DRAW     = 0;

// Wartosci materialowe (w centypionkach)
constexpr int PieceValue[PIECE_TYPE_NB] = {
    0, 100, 320, 330, 500, 900, 20000
};

// Material + PST wartosci dla incremental eval
// Indeksowane: [Piece][Square] — z perspektywy bialych
extern const int (*PSQ_MG)[SQUARE_NB];
extern const int (*PSQ_EG)[SQUARE_NB];

// Phase per piece type
constexpr int PhaseValue[PIECE_TYPE_NB] = {0, 0, 1, 1, 2, 4, 0};

// Inicjalizacja PSQ tablic (wywolaj raz na starcie)
void init_psq_tables();

// Ewaluacja pozycji (z perspektywy strony do ruchu)
int evaluate(const Position& pos);
