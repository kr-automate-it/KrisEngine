#pragma once

// === Tunowalne parametry silnika ===
// Eval: Texel tuning na TCEC, iteracja 3 (50K pozycji, 60K partii top silnikow).
// Search: SPSA-confirmed (3640 partii self-play) + Texel depth-1 cautious.
// MSE: 0.0917

struct Params {
    // --- Eval: mobilnosc (centypionki za pole) ---
    int knightMobMG = 3;
    int knightMobEG = 3;
    int bishopMobMG = 9;
    int bishopMobEG = 5;
    int rookMobMG   = 5;
    int rookMobEG   = 7;
    int queenMobMG  = 5;
    int queenMobEG  = 4;

    // --- Eval: passed pawns (skalowanie %, 100 = obecne wartosci) ---
    int passedPawnScale = 85;

    // --- Eval: king attack ---
    int kingAttackWeight = 8;

    // --- Eval: bishop pair ---
    int bishopPairMG = 40;
    int bishopPairEG = 60;

    // --- Eval: isolated/doubled pawns ---
    int isolatedPawnMG = 10;
    int isolatedPawnEG = 17;
    int doubledPawnMG  = 11;
    int doubledPawnEG  = 6;

    // --- Eval: knight outpost ---
    int knightOutpostMG = 25;
    int knightOutpostEG = 25;

    // --- Eval: rook ---
    int rookOpenFileMG  = 29;
    int rookOpenFileEG  = 21;
    int rook7thRankMG   = 25;
    int rook7thRankEG   = 30;

    // --- Eval: tempo ---
    int tempoMG = 21;
    int tempoEG = 11;

    // --- Search (SPSA-confirmed, nie zmieniaj przez Texel depth-1) ---
    int aspirationWindow   = 25;
    int nullMoveBaseR       = 3;
    int futilityMargin     = 200;
    int reverseFutilityMargin = 110;
    int razorMarginBase    = 300;
    int razorMarginPerDepth = 200;
    int lmpBase            = 3;
};

extern Params params;
