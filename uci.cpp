#include "uci.h"
#include "search.h"
#include "movegen.h"
#include "tt.h"
#include "zobrist.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

static const char* ENGINE_NAME = "ChessCpp";
static const char* ENGINE_AUTHOR = "rajda";

static Position pos;
static SearchInfo searchInfo;
static std::thread searchThread;
static StateInfo posStates[512];
static int posStateIdx = 0;

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

        if (movestogo > 0) {
            // Znana liczba ruchow do kontroli
            searchInfo.timeLimit = timeLeft / (movestogo + 1) + inc * 3 / 4;
        } else {
            // Klasyczna kontrola: rozdziel czas na ~25 ruchow
            // Ale uzywaj wiecej na poczatku (otwarcie/midgame wazniejsze)
            int movesLeft = 25;
            searchInfo.timeLimit = timeLeft / movesLeft + inc * 3 / 4;
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

    // Uruchom przeszukiwanie w osobnym watku
    if (searchThread.joinable())
        searchThread.join();

    searchThread = std::thread([&]() {
        SearchResult result = search(pos, searchInfo);
        std::cout << "bestmove " << pos.move_to_uci(result.bestMove) << std::endl;
    });
}

static void parse_position(std::istringstream& is) {
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
    TT.resize(64); // 64 MB tablicy transpozycji

    std::string line, token;

    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        is >> token;

        if (token == "uci") {
            std::cout << "id name " << ENGINE_NAME << std::endl;
            std::cout << "id author " << ENGINE_AUTHOR << std::endl;
            std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (token == "setoption") {
            // Parsuj "setoption name Hash value 128"
            std::string name, val;
            is >> name; // "name"
            is >> name; // actual name
            is >> val;  // "value"
            is >> val;  // actual value
            if (name == "Hash") {
                TT.resize(std::stoi(val));
            }
        }
        else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (token == "ucinewgame") {
            pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            TT.clear();
            TT.new_generation();
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
}
