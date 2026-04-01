#include "uci.h"
#include "search.h"
#include "movegen.h"
#include "tt.h"
#include "zobrist.h"
#include "params.h"
#include "book.h"
#include "bitboard.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// Makro do wypisywania opcji spin i parsowania setoption
#define TUNE_PARAM(name, var, lo, hi) \
    std::cout << "option name " #name " type spin default " << params.var \
              << " min " << lo << " max " << hi << std::endl;

static bool try_set_param(const std::string& name, int value) {
    // Eval
    if (name == "knightMobMG")   { params.knightMobMG = value; return true; }
    if (name == "knightMobEG")   { params.knightMobEG = value; return true; }
    if (name == "bishopMobMG")   { params.bishopMobMG = value; return true; }
    if (name == "bishopMobEG")   { params.bishopMobEG = value; return true; }
    if (name == "rookMobMG")     { params.rookMobMG = value; return true; }
    if (name == "rookMobEG")     { params.rookMobEG = value; return true; }
    if (name == "queenMobMG")    { params.queenMobMG = value; return true; }
    if (name == "queenMobEG")    { params.queenMobEG = value; return true; }
    if (name == "passedPawnScale") { params.passedPawnScale = value; return true; }
    if (name == "kingAttackWeight") { params.kingAttackWeight = value; return true; }
    if (name == "bishopPairMG")  { params.bishopPairMG = value; return true; }
    if (name == "bishopPairEG")  { params.bishopPairEG = value; return true; }
    if (name == "isolatedPawnMG") { params.isolatedPawnMG = value; return true; }
    if (name == "isolatedPawnEG") { params.isolatedPawnEG = value; return true; }
    if (name == "doubledPawnMG") { params.doubledPawnMG = value; return true; }
    if (name == "doubledPawnEG") { params.doubledPawnEG = value; return true; }
    if (name == "knightOutpostMG") { params.knightOutpostMG = value; return true; }
    if (name == "knightOutpostEG") { params.knightOutpostEG = value; return true; }
    if (name == "rookOpenFileMG") { params.rookOpenFileMG = value; return true; }
    if (name == "rookOpenFileEG") { params.rookOpenFileEG = value; return true; }
    if (name == "rook7thRankMG") { params.rook7thRankMG = value; return true; }
    if (name == "rook7thRankEG") { params.rook7thRankEG = value; return true; }
    if (name == "tempoMG")       { params.tempoMG = value; return true; }
    if (name == "tempoEG")       { params.tempoEG = value; return true; }
    // Search
    if (name == "aspirationWindow") { params.aspirationWindow = value; return true; }
    if (name == "nullMoveBaseR") { params.nullMoveBaseR = value; return true; }
    if (name == "futilityMargin") { params.futilityMargin = value; return true; }
    if (name == "reverseFutilityMargin") { params.reverseFutilityMargin = value; return true; }
    if (name == "razorMarginBase") { params.razorMarginBase = value; return true; }
    if (name == "razorMarginPerDepth") { params.razorMarginPerDepth = value; return true; }
    if (name == "lmpBase") { params.lmpBase = value; return true; }
    return false;
}

static const char* ENGINE_NAME = "ChessCpp";
static const char* ENGINE_AUTHOR = "rajda";

static Position pos;
static SearchInfo searchInfo;
static std::thread searchThread;
static StateInfo posStates[512];
static int posStateIdx = 0;
static int numThreads = 1;

static void parse_go(std::istringstream& is) {
    std::string token;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    int depth = 64;
    int movetime = 0;
    bool infinite = false;

    while (is >> token) {
        if (token == "wtime")     is >> wtime;
        else if (token == "btime")     is >> btime;
        else if (token == "winc")      is >> winc;
        else if (token == "binc")      is >> binc;
        else if (token == "movestogo") is >> movestogo;
        else if (token == "depth")     is >> depth;
        else if (token == "movetime")  is >> movetime;
        else if (token == "infinite")  infinite = true;
    }

    searchInfo.maxDepth = depth;
    if (movetime > 0) {
        searchInfo.timeLimit = movetime;
    } else if (!infinite && (wtime > 0 || btime > 0)) {
        int timeLeft = (pos.side_to_move() == WHITE) ? wtime : btime;
        int inc      = (pos.side_to_move() == WHITE) ? winc  : binc;

        // Phase-aware movesLeft: wiecej czasu w midgame, mniej w endgame
        // Phase = ilosc figur (bez pionkow i kroli)
        int nonPawnPieces = popcount(pos.pieces(KNIGHT)) + popcount(pos.pieces(BISHOP))
                          + popcount(pos.pieces(ROOK)) * 2 + popcount(pos.pieces(QUEEN)) * 4;
        // nonPawnPieces: max ~24 (poczatek), 0 (endgame)

        if (movestogo > 0) {
            searchInfo.timeLimit = timeLeft / (movestogo + 1) + inc * 3 / 4;
        } else {
            // Im wiecej figur (midgame), tym mniej ruchow zakladamy (wiecej czasu/ruch)
            // Endgame: 30 ruchow, midgame: 20 ruchow
            int movesLeft = 30 - nonPawnPieces / 3; // ~20-30
            if (movesLeft < 15) movesLeft = 15;
            if (movesLeft > 35) movesLeft = 35;
            searchInfo.timeLimit = timeLeft / movesLeft + inc * 3 / 4;
        }

        // Bonus czasu w krytycznych pozycjach (szach, duzo figur w centrum)
        if (pos.in_check()) {
            searchInfo.timeLimit = searchInfo.timeLimit * 3 / 2; // +50% gdy w szachu
        }

        // Safety margins
        if (searchInfo.timeLimit < 10)
            searchInfo.timeLimit = 10;
        // Nigdy nie uzywaj wiecej niz 40% pozostalego czasu
        int64_t maxTime = (int64_t)timeLeft * 2 / 5;
        if (searchInfo.timeLimit > maxTime)
            searchInfo.timeLimit = std::max((int64_t)10, maxTime);
        // Zostawaj bufor 50ms na komunikacje
        if (searchInfo.timeLimit > timeLeft - 50)
            searchInfo.timeLimit = std::max((int64_t)10, (int64_t)(timeLeft - 50));
    } else {
        searchInfo.timeLimit = 0;
    }

    // Book probe — natychmiastowa odpowiedz jesli mamy ruch w ksiazce
    Move bookMove = book.probe(pos);
    if (bookMove != MOVE_NONE) {
        std::cout << "bestmove " << pos.move_to_uci(bookMove) << std::endl;
        return;
    }

    // Uruchom przeszukiwanie w osobnym watku
    if (searchThread.joinable())
        searchThread.join();

    searchThread = std::thread([&]() {
        SearchResult result = search_smp(pos, searchInfo, numThreads);
        std::string bmStr = "bestmove " + pos.move_to_uci(result.bestMove);
        if (result.ponderMove != MOVE_NONE) {
            // Wyslij ponder move — GUI moze uzyc go ponder
            StateInfo tmpSt;
            pos.do_move(result.bestMove, tmpSt);
            if (pos.is_legal(result.ponderMove))
                bmStr += " ponder " + pos.move_to_uci(result.ponderMove);
            pos.undo_move(result.bestMove);
        }
        std::cout << bmStr << std::endl;
    });
}

static void parse_position(std::istringstream& is) {
    // Zatrzymaj search jesli jeszcze trwa!
    searchInfo.stopped.store(true);
    if (searchThread.joinable())
        searchThread.join();

    std::string token;
    is >> token;

    if (token == "startpos") {
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        is >> token; // consume "moves" if present
    } else if (token == "fen") {
        std::string fen;
        while (is >> token && token != "moves")
            fen += token + " ";
        pos.set(fen);
    }

    // Parsuj ruchy
    posStateIdx = 0;
    while (is >> token) {
        Move m = pos.parse_uci(token);
        if (m != MOVE_NONE) {
            pos.do_move(m, posStates[posStateIdx++]);
        }
    }
}

void uci_loop() {
    init_bitboards();
    Zobrist::init();
    // Laduj ksiazke debiutowa jesli istnieje
    if (book.load("book.bin"))
        ; // OK
    else
        book.load("C:/Users/rajda/chess_cpp/book.bin"); // fallback
    TT.resize(64); // 64 MB tablicy transpozycji

    std::string line, token;

    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream is(line);
        if (!(is >> token)) continue;

        if (token == "uci") {
            std::cout << "id name " << ENGINE_NAME << std::endl;
            std::cout << "id author " << ENGINE_AUTHOR << std::endl;
            std::cout << "option name OwnBook type check default true" << std::endl;
            std::cout << "option name BookFile type string default book.bin" << std::endl;
            std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
            std::cout << "option name Threads type spin default 1 min 1 max 8" << std::endl;
            // Tunable params
            TUNE_PARAM(knightMobMG, knightMobMG, 0, 10)
            TUNE_PARAM(knightMobEG, knightMobEG, 0, 10)
            TUNE_PARAM(bishopMobMG, bishopMobMG, 0, 10)
            TUNE_PARAM(bishopMobEG, bishopMobEG, 0, 10)
            TUNE_PARAM(rookMobMG, rookMobMG, 0, 10)
            TUNE_PARAM(rookMobEG, rookMobEG, 0, 10)
            TUNE_PARAM(queenMobMG, queenMobMG, 0, 5)
            TUNE_PARAM(queenMobEG, queenMobEG, 0, 5)
            TUNE_PARAM(passedPawnScale, passedPawnScale, 50, 200)
            TUNE_PARAM(kingAttackWeight, kingAttackWeight, 0, 20)
            TUNE_PARAM(bishopPairMG, bishopPairMG, 0, 80)
            TUNE_PARAM(bishopPairEG, bishopPairEG, 0, 100)
            TUNE_PARAM(isolatedPawnMG, isolatedPawnMG, 0, 30)
            TUNE_PARAM(isolatedPawnEG, isolatedPawnEG, 0, 30)
            TUNE_PARAM(doubledPawnMG, doubledPawnMG, 0, 20)
            TUNE_PARAM(doubledPawnEG, doubledPawnEG, 0, 20)
            TUNE_PARAM(knightOutpostMG, knightOutpostMG, 0, 50)
            TUNE_PARAM(knightOutpostEG, knightOutpostEG, 0, 30)
            TUNE_PARAM(rookOpenFileMG, rookOpenFileMG, 0, 40)
            TUNE_PARAM(rookOpenFileEG, rookOpenFileEG, 0, 30)
            TUNE_PARAM(rook7thRankMG, rook7thRankMG, 0, 40)
            TUNE_PARAM(rook7thRankEG, rook7thRankEG, 0, 50)
            TUNE_PARAM(tempoMG, tempoMG, 0, 30)
            TUNE_PARAM(tempoEG, tempoEG, 0, 15)
            TUNE_PARAM(aspirationWindow, aspirationWindow, 10, 100)
            TUNE_PARAM(nullMoveBaseR, nullMoveBaseR, 2, 5)
            TUNE_PARAM(futilityMargin, futilityMargin, 50, 400)
            TUNE_PARAM(reverseFutilityMargin, reverseFutilityMargin, 50, 250)
            TUNE_PARAM(razorMarginBase, razorMarginBase, 100, 500)
            TUNE_PARAM(razorMarginPerDepth, razorMarginPerDepth, 50, 400)
            TUNE_PARAM(lmpBase, lmpBase, 1, 8)
            std::cout << "uciok" << std::endl;
        }
        else if (token == "setoption") {
            std::string name, val;
            is >> name; // "name"
            is >> name; // actual name
            is >> val;  // "value"
            is >> val;  // actual value
            if (name == "Threads") {
                numThreads = std::max(1, std::min(8, std::stoi(val)));
            } else if (name == "Hash") {
                TT.resize(std::stoi(val));
            } else if (name == "OwnBook") {
                book.enabled = (val == "true");
            } else if (name == "BookFile") {
                book.load(val);
            } else {
                try_set_param(name, std::stoi(val));
            }
        }
        else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (token == "ucinewgame") {
            pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            TT.clear();
            TT.new_generation();
            clear_search_tables();
        }
        else if (token == "position") {
            parse_position(is);
        }
        else if (token == "go") {
            parse_go(is);
        }
        else if (token == "stop") {
            searchInfo.stopped.store(true);
            if (searchThread.joinable())
                searchThread.join();
        }
        else if (token == "quit") {
            searchInfo.stopped.store(true);
            if (searchThread.joinable())
                searchThread.join();
            break;
        }
    }

    // Cleanup: jesli stdin sie zamknal bez "quit"
    searchInfo.stopped.store(true);
    if (searchThread.joinable())
        searchThread.join();
}
