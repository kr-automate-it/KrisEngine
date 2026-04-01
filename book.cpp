#include "book.h"
#include "polyglot_keys.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>

Book book;

Book::~Book() {
    delete[] data;
}

// Czytaj 16 bajtow big-endian z pliku
static U64 read_u64_be(const uint8_t* p) {
    U64 v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}
static uint16_t read_u16_be(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | p[1];
}
static uint32_t read_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool Book::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = file.tellg();
    if (fileSize < 16 || fileSize % 16 != 0) return false;

    file.seekg(0);
    std::vector<uint8_t> raw(fileSize);
    file.read(reinterpret_cast<char*>(raw.data()), fileSize);

    numEntries = fileSize / 16;
    delete[] data;
    data = new Entry[numEntries];

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* p = raw.data() + i * 16;
        data[i].key    = read_u64_be(p);
        data[i].move   = read_u16_be(p + 8);
        data[i].weight = read_u16_be(p + 10);
        data[i].learn  = read_u32_be(p + 12);
    }

    return true;
}

// Polyglot hash — obliczany od zera dla pozycji.
// Uzywa INNYCH losowych liczb niz nasz Zobrist (standard Polyglot).
U64 Book::polyglot_hash(const Position& pos) const {
    U64 h = 0;

    // Figury
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos.piece_on(Square(sq));
        if (p == NO_PIECE) continue;

        // Polyglot piece index: BP=0, WP=1, BN=2, WN=3, BB=4, WB=5, BR=6, WR=7, BQ=8, WQ=9, BK=10, WK=11
        int pgPiece;
        PieceType pt = type_of(p);
        Color c = color_of(p);
        // Mapowanie: (pt-1)*2 + (c==WHITE ? 1 : 0)
        //   PAWN(1) -> 0,1  KNIGHT(2) -> 2,3  BISHOP(3) -> 4,5  ROOK(4) -> 6,7  QUEEN(5) -> 8,9  KING(6) -> 10,11
        pgPiece = (pt - 1) * 2 + (c == WHITE ? 1 : 0);

        h ^= PolyglotKeys[pgPiece * 64 + sq];
    }

    // Roszada
    CastlingRight cr = pos.castling_rights();
    if (cr & WHITE_OO)  h ^= PolyglotKeys[768 + 0];
    if (cr & WHITE_OOO) h ^= PolyglotKeys[768 + 1];
    if (cr & BLACK_OO)  h ^= PolyglotKeys[768 + 2];
    if (cr & BLACK_OOO) h ^= PolyglotKeys[768 + 3];

    // En passant (tylko jesli jest pionek mogacy bic)
    if (pos.ep_square() != SQ_NONE) {
        int epFile = file_of(pos.ep_square());
        // Polyglot: en passant tylko jesli jest wrog pionek mogacy bic
        Color them = ~pos.side_to_move();
        bool canCapture = PawnAttacks[them][pos.ep_square()] & pos.pieces(pos.side_to_move(), PAWN);
        if (canCapture)
            h ^= PolyglotKeys[772 + epFile];
    }

    // Strona do ruchu (Polyglot: XOR jesli bialy)
    if (pos.side_to_move() == WHITE)
        h ^= PolyglotKeys[780];

    return h;
}

Move Book::convert_move(const Position& pos, uint16_t pgMove) const {
    // Polyglot move encoding:
    // bits 0-2:   to file
    // bits 3-5:   to row
    // bits 6-8:   from file
    // bits 9-11:  from row
    // bits 12-14: promotion (0=none, 1=knight, 2=bishop, 3=rook, 4=queen)
    int toFile   = (pgMove >> 0) & 7;
    int toRow    = (pgMove >> 3) & 7;
    int fromFile = (pgMove >> 6) & 7;
    int fromRow  = (pgMove >> 9) & 7;
    int promo    = (pgMove >> 12) & 7;

    Square from = make_square(fromFile, fromRow);
    Square to   = make_square(toFile, toRow);

    // Polyglot koduje roszade jako krol -> wieza
    // Musimy przerobic na krol -> pole docelowe (nasz format)
    if (type_of(pos.piece_on(from)) == KING) {
        if (from == SQ_E1 && to == SQ_H1) to = SQ_G1; // White O-O
        if (from == SQ_E1 && to == SQ_A1) to = SQ_C1; // White O-O-O
        if (from == SQ_E8 && to == SQ_H8) to = SQ_G8; // Black O-O
        if (from == SQ_E8 && to == SQ_A8) to = SQ_C8; // Black O-O-O
    }

    // Uzyj parse_uci z pozycji — to wykryje roszade, en passant, promocje
    std::string uci = square_to_str(from) + square_to_str(to);
    if (promo > 0) {
        const char promoChars[] = " nbrq";
        uci += promoChars[promo];
    }

    return pos.parse_uci(uci);
}

Move Book::probe(const Position& pos) const {
    if (!data || !enabled) return MOVE_NONE;

    U64 key = polyglot_hash(pos);

    // Binary search — plik jest posortowany po key
    size_t lo = 0, hi = numEntries;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (data[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }

    // Zbierz wszystkie ruchy z tym kluczem
    struct BookMove { uint16_t move; uint16_t weight; };
    BookMove candidates[32];
    int count = 0;
    uint32_t totalWeight = 0;

    for (size_t i = lo; i < numEntries && data[i].key == key && count < 32; i++) {
        candidates[count] = {data[i].move, data[i].weight};
        totalWeight += data[i].weight;
        count++;
    }

    if (count == 0) return MOVE_NONE;

    // Losowy wybor wazony — lepsze ruchy maja wieksza szanse
    // Seed: uzywamy hash pozycji + zegar (rozne partie = rozne wybory)
    static uint64_t rng = 0;
    rng ^= key;
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    uint32_t roll = (uint32_t)(rng >> 32) % totalWeight;

    uint16_t chosen = candidates[0].move;
    uint32_t cumulative = 0;
    for (int i = 0; i < count; i++) {
        cumulative += candidates[i].weight;
        if (roll < cumulative) {
            chosen = candidates[i].move;
            break;
        }
    }

    Move m = convert_move(pos, chosen);

    // Weryfikuj ze ruch jest legalny
    if (m != MOVE_NONE && pos.is_legal(m))
        return m;

    return MOVE_NONE;
}
