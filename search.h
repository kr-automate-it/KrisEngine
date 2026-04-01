#pragma once
#include "position.h"
#include "eval.h"
#include <atomic>
#include <chrono>

struct SearchInfo {
    int depth;
    int maxDepth;
    std::atomic<int64_t> nodes; // atomic — bezpieczne dla SMP
    std::chrono::steady_clock::time_point startTime;
    int64_t timeLimit; // ms, 0 = brak limitu
    std::atomic<bool> stopped;

    SearchInfo() : depth(0), maxDepth(64), nodes(0), timeLimit(0), stopped(false) {}
};

struct SearchResult {
    Move bestMove;
    Move ponderMove; // przewidywany ruch przeciwnika (do pondering)
    int  score;
};

SearchResult search(Position& pos, SearchInfo& info);
// Lazy SMP: uruchamia numThreads watkow, kazdy szuka na kopii pozycji,
// wspoldzielac TT. Watek glowny zwraca wynik.
SearchResult search_smp(Position& pos, SearchInfo& info, int numThreads);
void clear_search_tables();
