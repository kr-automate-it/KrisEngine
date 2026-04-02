#pragma once

// === Tunowalne parametry silnika ===
// Eval: Texel tuning na TCEC, iteracja 3 (50K pozycji, 60K partii top silnikow).
// Search: SPSA-confirmed (3640 partii self-play) + Texel depth-1 cautious.
// MSE: 0.0917

struct Params {
    // --- Eval: mobilnosc (centypionki za pole) ---
    // Texel tuned (TCEC S24 League 2, 10K pozycji, 10 iteracji)
    int knightMobMG = 5;
    int knightMobEG = 9;
    int bishopMobMG = 10;
    int bishopMobEG = 0;
    int rookMobMG   = 6;
    int rookMobEG   = 10;
    int queenMobMG  = 5;
    int queenMobEG  = 5;

    // --- Eval: passed pawns (skalowanie %, 100 = obecne wartosci) ---
    int passedPawnScale = 100;

    // --- Eval: king attack ---
    int kingAttackWeight = 8;

    // --- Eval: bishop pair ---
    int bishopPairMG = 80;
    int bishopPairEG = 100;

    // --- Eval: isolated/doubled pawns ---
    int isolatedPawnMG = 24;
    int isolatedPawnEG = 29;
    int doubledPawnMG  = 5;
    int doubledPawnEG  = 8;

    // --- Eval: knight outpost ---
    int knightOutpostMG = 0;
    int knightOutpostEG = 0;

    // --- Eval: rook ---
    int rookOpenFileMG  = 2;
    int rookOpenFileEG  = 0;
    int rook7thRankMG   = 15;
    int rook7thRankEG   = 25;

    // --- Eval: backward pawn ---
    int backwardPawnMG = 8;
    int backwardPawnEG = 12;

    // --- Eval: connected pawns ---
    int connectedPawnMG = 5;
    int connectedPawnEG = 8;

    // --- Eval: bad bishop (per blocking pawn) ---
    int badBishopMG = 4;
    int badBishopEG = 6;

    // --- Eval: king centralization (endgame) ---
    int kingCentralEG = 10;

    // --- Eval: hanging pieces ---
    int hangingPieceMG = 15;
    int hangingPieceEG = 20;

    // --- Eval: tempo ---
    int tempoMG = 29;
    int tempoEG = 15;

    // --- Search (SPSA) ---
    int aspirationWindow   = 25;
    int nullMoveBaseR       = 3;
    int futilityMargin     = 194;
    int reverseFutilityMargin = 113;
    int razorMarginBase    = 287;
    int razorMarginPerDepth = 196;
    int lmpBase            = 3;
    int lmrDivisor         = 225; // /100 = 2.25 w formule LMR
    int qsearchDelta       = 250; // margines delta pruning w qsearch
    int probCutMargin      = 100; // beta offset dla ProbCut
};

extern Params params;
