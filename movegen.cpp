#include "movegen.h"

static void generate_pawn_moves(const Position& pos, MoveList& list, U64 target) {
    Color us = pos.side_to_move();
    Color them = ~us;
    int up = (us == WHITE) ? 8 : -8;
    U64 rank3 = (us == WHITE) ? Rank3_BB : Rank6_BB;
    U64 rank7 = (us == WHITE) ? Rank7_BB : Rank2_BB;
    U64 empty = ~pos.pieces();
    U64 enemies = pos.pieces(them);
    U64 pawns = pos.pieces(us, PAWN);

    // Pionki na przedostatnim rzedzie (promocje)
    U64 promoters = pawns & rank7;
    U64 non_promoters = pawns & ~rank7;

    // Pojedyncze przesuniecia (nie-promocje)
    U64 push1 = (us == WHITE) ? (non_promoters << 8) : (non_promoters >> 8);
    push1 &= empty;

    // Podwojne przesuniecia
    U64 push2 = (us == WHITE) ? ((push1 & rank3) << 8) : ((push1 & rank3) >> 8);
    push2 &= empty;

    push1 &= target;
    push2 &= target;

    while (push1) {
        Square to = pop_lsb(push1);
        list.add(make_move(Square(to - up), to));
    }
    while (push2) {
        Square to = pop_lsb(push2);
        list.add(make_move(Square(to - 2 * up), to));
    }

    // Bicia (nie-promocje)
    U64 captureTarget = enemies & target;
    U64 captL = PawnAttacks[us][0]; // placeholder
    // Oblicz ataki pionkow ruchem w lewo i prawo
    for (U64 bb = non_promoters; bb; ) {
        Square from = pop_lsb(bb);
        U64 attacks = PawnAttacks[us][from] & captureTarget;
        while (attacks) {
            Square to = pop_lsb(attacks);
            list.add(make_move(from, to));
        }
    }

    // Promocje
    if (promoters) {
        // Przesuniecia z promocja
        U64 promoPush = (us == WHITE) ? (promoters << 8) : (promoters >> 8);
        promoPush &= empty & target;

        while (promoPush) {
            Square to = pop_lsb(promoPush);
            Square from = Square(to - up);
            list.add(make_move(from, to, PROMOTION, QUEEN));
            list.add(make_move(from, to, PROMOTION, ROOK));
            list.add(make_move(from, to, PROMOTION, BISHOP));
            list.add(make_move(from, to, PROMOTION, KNIGHT));
        }

        // Bicia z promocja
        for (U64 bb = promoters; bb; ) {
            Square from = pop_lsb(bb);
            U64 attacks = PawnAttacks[us][from] & enemies & target;
            while (attacks) {
                Square to = pop_lsb(attacks);
                list.add(make_move(from, to, PROMOTION, QUEEN));
                list.add(make_move(from, to, PROMOTION, ROOK));
                list.add(make_move(from, to, PROMOTION, BISHOP));
                list.add(make_move(from, to, PROMOTION, KNIGHT));
            }
        }
    }

    // En passant
    if (pos.ep_square() != SQ_NONE) {
        U64 epPawns = non_promoters & PawnAttacks[them][pos.ep_square()];
        while (epPawns) {
            Square from = pop_lsb(epPawns);
            list.add(make_move(from, pos.ep_square(), EN_PASSANT));
        }
    }
}

static void generate_piece_moves(const Position& pos, MoveList& list, PieceType pt, U64 target) {
    Color us = pos.side_to_move();
    U64 bb = pos.pieces(us, pt);
    U64 occ = pos.pieces();

    while (bb) {
        Square from = pop_lsb(bb);
        U64 attacks = 0;

        switch (pt) {
            case KNIGHT: attacks = KnightAttacks[from]; break;
            case BISHOP: attacks = bishop_attacks(from, occ); break;
            case ROOK:   attacks = rook_attacks(from, occ);   break;
            case QUEEN:  attacks = queen_attacks(from, occ);  break;
            case KING:   attacks = KingAttacks[from];  break;
            default: break;
        }

        attacks &= target;

        while (attacks) {
            Square to = pop_lsb(attacks);
            list.add(make_move(from, to));
        }
    }
}

static void generate_castling(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    U64 occ = pos.pieces();

    if (us == WHITE) {
        if ((pos.castling_rights() & WHITE_OO)
            && !(occ & (square_bb(SQ_F1) | square_bb(SQ_G1)))) {
            list.add(make_move(SQ_E1, SQ_G1, CASTLING));
        }
        if ((pos.castling_rights() & WHITE_OOO)
            && !(occ & (square_bb(SQ_D1) | square_bb(SQ_C1) | square_bb(SQ_B1)))) {
            list.add(make_move(SQ_E1, SQ_C1, CASTLING));
        }
    } else {
        if ((pos.castling_rights() & BLACK_OO)
            && !(occ & (square_bb(SQ_F8) | square_bb(SQ_G8)))) {
            list.add(make_move(SQ_E8, SQ_G8, CASTLING));
        }
        if ((pos.castling_rights() & BLACK_OOO)
            && !(occ & (square_bb(SQ_D8) | square_bb(SQ_C8) | square_bb(SQ_B8)))) {
            list.add(make_move(SQ_E8, SQ_C8, CASTLING));
        }
    }
}

void generate_moves(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    // Nie generuj bic krola przeciwnika (krol nigdy nie jest brany)
    U64 target = ~pos.pieces(us) & ~pos.pieces(~us, KING);

    generate_pawn_moves(pos, list, ~pos.pieces(~us, KING));
    generate_piece_moves(pos, list, KNIGHT, target);
    generate_piece_moves(pos, list, BISHOP, target);
    generate_piece_moves(pos, list, ROOK, target);
    generate_piece_moves(pos, list, QUEEN, target);
    generate_piece_moves(pos, list, KING, target & ~pos.pieces(KING));
    generate_castling(pos, list);
}

void generate_captures(const Position& pos, MoveList& list) {
    Color us = pos.side_to_move();
    Color them = ~us;
    U64 target = pos.pieces(them) & ~pos.pieces(them, KING);

    generate_pawn_moves(pos, list, target);
    generate_piece_moves(pos, list, KNIGHT, target);
    generate_piece_moves(pos, list, BISHOP, target);
    generate_piece_moves(pos, list, ROOK, target);
    generate_piece_moves(pos, list, QUEEN, target);
    generate_piece_moves(pos, list, KING, target);
}
