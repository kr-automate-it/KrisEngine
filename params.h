#pragma once

// === Tunowalne parametry silnika ===
// Eval: Texel tuning na TCEC, iteracja 3 (50K pozycji, 60K partii top silnikow).
// Search: SPSA-confirmed (3640 partii self-play) + Texel depth-1 cautious.
// MSE: 0.0917

struct Params {
    // --- Eval: mobilnosc (centypionki za pole) ---
    // Texel tuned (TCEC S28 League 2, 20K pozycji)
    int knightMobMG = 5;
    int knightMobEG = 0;
    int bishopMobMG = 8;
    int bishopMobEG = 2;
    int rookMobMG   = 3;
    int rookMobEG   = 10;
    int queenMobMG  = 5;
    int queenMobEG  = 5;

    // --- Eval: passed pawns (skalowanie %, 100 = obecne wartosci) ---
    int passedPawnScale = 75;

    // --- Eval: king attack ---
    int kingAttackWeight = 8;

    // --- Eval: bishop pair ---
    int bishopPairMG = 55;
    int bishopPairEG = 75;

    // --- Eval: isolated/doubled pawns ---
    int isolatedPawnMG = 8;
    int isolatedPawnEG = 17;
    int doubledPawnMG  = 11;
    int doubledPawnEG  = 12;

    // --- Eval: knight outpost ---
    int knightOutpostMG = 15;
    int knightOutpostEG = 15;

    // --- Eval: rook ---
    int rookOpenFileMG  = 35;
    int rookOpenFileEG  = 21;
    int rook7thRankMG   = 20;
    int rook7thRankEG   = 25;

    // --- Eval: tempo ---
    int tempoMG = 23;
    int tempoEG = 15;

    // --- Search (SPSA-confirmed, nie zmieniaj przez Texel depth-1) ---
    int aspirationWindow   = 25;
    int nullMoveBaseR       = 3;
    int futilityMargin     = 200;
    int reverseFutilityMargin = 110;
    int razorMarginBase    = 300;
    int razorMarginPerDepth = 200;
    int lmpBase            = 4;
};

extern Params params;
