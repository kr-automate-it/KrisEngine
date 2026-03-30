#pragma once
#include "position.h"
#include "eval.h"
#include <atomic>
#include <chrono>

struct SearchInfo {
    int depth;
    int maxDepth;
    int64_t nodes;
    std::chrono::steady_clock::time_point startTime;
    int64_t timeLimit; // ms, 0 = brak limitu
    std::atomic<bool> stopped;

    SearchInfo() : depth(0), maxDepth(64), nodes(0), timeLimit(0), stopped(false) {}
};

struct SearchResult {
    Move bestMove;
    int  score;
};

SearchResult search(Position& pos, SearchInfo& info);
