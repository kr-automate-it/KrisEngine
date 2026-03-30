#pragma once
#include <cstdint>
#include <string>

// === Podstawowe typy ===

using U64 = uint64_t;
using Square = int;
using Move = uint16_t; // 16-bitowy ruch: 6 bit from, 6 bit to, 4 bit flags

enum Color : int { WHITE, BLACK, COLOR_NB = 2 };

enum PieceType : int {
    NO_PIECE_TYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    PIECE_TYPE_NB = 7
};

enum Piece : int {
    NO_PIECE,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece make_piece(Color c, PieceType pt) {
    return Piece((c << 3) | pt);
}

constexpr PieceType type_of(Piece p) {
    return PieceType(p & 7);
}

constexpr Color color_of(Piece p) {
    return Color(p >> 3);
}

// === Kwadraty ===
// A1=0, B1=1, ... H1=7, A2=8, ... H8=63

enum : Square {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

constexpr int rank_of(Square s) { return s >> 3; }
constexpr int file_of(Square s) { return s & 7; }
constexpr Square make_square(int file, int rank) { return Square(rank * 8 + file); }

inline constexpr Color operator~(Color c) { return Color(c ^ 1); }

// === Ruch (16 bit) ===
// bity 0-5:  from square
// bity 6-11: to square
// bity 12-13: flaga promocji (0=knight, 1=bishop, 2=rook, 3=queen)
// bity 14-15: typ specjalny (0=normal, 1=promocja, 2=en passant, 3=roszada)

enum MoveType : int {
    NORMAL     = 0,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14
};

constexpr Move make_move(Square from, Square to) {
    return Move(from | (to << 6));
}

constexpr Move make_move(Square from, Square to, MoveType type, PieceType promo = KNIGHT) {
    return Move(from | (to << 6) | type | ((promo - KNIGHT) << 12));
}

constexpr Square move_from(Move m) { return Square(m & 0x3F); }
constexpr Square move_to(Move m)   { return Square((m >> 6) & 0x3F); }
constexpr MoveType move_type(Move m) { return MoveType(m & (3 << 14)); }
constexpr PieceType promo_type(Move m) { return PieceType(((m >> 12) & 3) + KNIGHT); }

constexpr Move MOVE_NONE = 0;

// === Roszada ===
enum CastlingRight : int {
    NO_CASTLING,
    WHITE_OO  = 1,
    WHITE_OOO = 2,
    BLACK_OO  = 4,
    BLACK_OOO = 8,
    ALL_CASTLING = 15
};

inline CastlingRight operator|(CastlingRight a, CastlingRight b) {
    return CastlingRight(int(a) | int(b));
}
inline CastlingRight& operator|=(CastlingRight& a, CastlingRight b) {
    return a = a | b;
}
inline CastlingRight operator&(CastlingRight a, CastlingRight b) {
    return CastlingRight(int(a) & int(b));
}
inline CastlingRight& operator&=(CastlingRight& a, CastlingRight b) {
    return a = a & b;
}
inline CastlingRight operator~(CastlingRight a) {
    return CastlingRight(~int(a) & 15);
}

// Konwersje kwadrat <-> string
inline std::string square_to_str(Square s) {
    return std::string(1, 'a' + file_of(s)) + std::string(1, '1' + rank_of(s));
}

inline Square str_to_square(const std::string& s) {
    return make_square(s[0] - 'a', s[1] - '1');
}
