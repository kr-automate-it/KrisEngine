#pragma once

// === Tunowalne parametry silnika ===
// Wagi z Texel tuningu na TCEC, iteracja 2 (50K pozycji, 60K partii top silnikow).
// MSE: 0.0921

struct Params {
    // --- Eval: mobilnosc (centypionki za pole) ---
    int knightMobMG = 3;
    int knightMobEG = 3;
    int bishopMobMG = 8;
    int bishopMobEG = 5;
    int rookMobMG   = 4;
    int rookMobEG   = 6;
    int queenMobMG  = 4;
    int queenMobEG  = 3;

    // --- Eval: passed pawns (skalowanie %, 100 = obecne wartosci) ---
    int passedPawnScale = 90;

    // --- Eval: king attack ---
    int kingAttackWeight = 8;

    // --- Eval: bishop pair ---
    int bishopPairMG = 40;
    int bishopPairEG = 55;

    // --- Eval: isolated/doubled pawns ---
    int isolatedPawnMG = 8;
    int isolatedPawnEG = 17;
    int doubledPawnMG  = 9;
    int doubledPawnEG  = 8;

    // --- Eval: knight outpost ---
    int knightOutpostMG = 25;
    int knightOutpostEG = 20;

    // --- Eval: rook ---
    int rookOpenFileMG  = 26;
    int rookOpenFileEG  = 18;
    int rook7thRankMG   = 30;
    int rook7thRankEG   = 30;

    // --- Eval: tempo ---
    int tempoMG = 19;
    int tempoEG = 9;

    // --- Search ---
    int aspirationWindow   = 25;
    int nullMoveBaseR       = 3;
    int futilityMargin     = 200;
    int reverseFutilityMargin = 110;
    int razorMarginBase    = 300;
    int razorMarginPerDepth = 200;
    int lmpBase            = 3;
};

extern Params params;
