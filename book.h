#pragma once
#include "types.h"
#include "position.h"
#include <string>

// Polyglot opening book — natychmiastowe otwarcia zamiast tracenia czasu.
// Format .bin: posortowana tablica 16-bajtowych wpisow (key + move + weight + learn).

class Book {
public:
    Book() : data(nullptr), numEntries(0), enabled(true) {}
    ~Book();

    bool load(const std::string& path);
    bool is_loaded() const { return data != nullptr; }

    // Probe: zwraca najlepszy ruch z ksiazki lub MOVE_NONE
    Move probe(const Position& pos) const;

    bool enabled;

private:
    struct Entry {
        U64      key;
        uint16_t move;
        uint16_t weight;
        uint32_t learn;
    };

    Entry* data;
    size_t numEntries;

    // Polyglot hash — osobny od naszego Zobrist (inne losowe liczby)
    U64 polyglot_hash(const Position& pos) const;

    // Konwersja ruchu Polyglot -> nasz format
    Move convert_move(const Position& pos, uint16_t pgMove) const;
};

extern Book book;
