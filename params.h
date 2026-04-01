#pragma once

// === Tunowalne parametry silnika ===
// Eval: Texel tuning na TCEC, iteracja 3 (50K pozycji, 60K partii top silnikow).
// Search: SPSA-confirmed (3640 partii self-play) + Texel depth-1 cautious.
// MSE: 0.0917

struct Params {
    // --- Eval: mobilnosc (centypionki za pole) ---
    // Texel tuned (TCEC S26 League 2, 20K pozycji, 8 iteracji)
    int knightMobMG = 4;
    int knightMobEG = 0;
    int bishopMobMG = 10;
    int bishopMobEG = 7;
    int rookMobMG   = 6;
    int rookMobEG   = 5;
    int queenMobMG  = 5;
    int queenMobEG  = 5;

    // --- Eval: passed pawns (skalowanie %, 100 = obecne wartosci) ---
    int passedPawnScale = 50;

    // --- Eval: king attack ---
    int kingAttackWeight = 8;

    // --- Eval: bishop pair ---
    int bishopPairMG = 80;
    int bishopPairEG = 100;

    // --- Eval: isolated/doubled pawns ---
    int isolatedPawnMG = 4;
    int isolatedPawnEG = 29;
    int doubledPawnMG  = 13;
    int doubledPawnEG  = 16;

    // --- Eval: knight outpost ---
    int knightOutpostMG = 15;
    int knightOutpostEG = 0;

    // --- Eval: rook ---
    int rookOpenFileMG  = 20;
    int rookOpenFileEG  = 3;
    int rook7thRankMG   = 0;
    int rook7thRankEG   = 0;

    // --- Eval: tempo ---
    int tempoMG = 29;
    int tempoEG = 15;

    // --- Search (SPSA-confirmed, nie zmieniaj przez Texel depth-1) ---
    int aspirationWindow   = 25;
    int nullMoveBaseR       = 5;
    int futilityMargin     = 200;
    int reverseFutilityMargin = 110;
    int razorMarginBase    = 300;
    int razorMarginPerDepth = 200;
    int lmpBase            = 7;
};

extern Params params;
