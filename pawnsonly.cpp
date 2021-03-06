// Copyright (C) 2016  Sami Liedes
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "MemTranspositionTable.hpp"
#include "binom.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <thread>
#include <vector>

static constexpr bool DEBUG = true;

// static constexpr int N = 7;
// static constexpr int VERBOSE_DEPTH = 3;
// static constexpr int PARALLEL_DEPTH = 10;
// static constexpr int CUT_MIN_DEPTH = 0;
// static constexpr int PARALLEL_MIN_DEPTH = 0;
// static constexpr size_t TP_TABLE_SIZE = 671088637; // 2.5 gigabytes

static constexpr int N = 8;
static constexpr int VERBOSE_DEPTH = 8;
static constexpr int PARALLEL_DEPTH = 18;
static constexpr int CUT_MIN_DEPTH = 4;
static constexpr int PARALLEL_MIN_DEPTH = 3;
static constexpr size_t TP_TABLE_SIZE = 6710886419; // 25 gigabytes

static constexpr int NUM_THREADS = 8;

//#define SAVE_NODES_LIMIT 50
//static constexpr int SAVE_LEVELS = 1;

// # of 8-byte elements; try to choose a prime
//static constexpr size_t TP_TABLE_SIZE = 30146531; // 115 megabytes
//static constexpr size_t TP_TABLE_SIZE = 134217689; // .5 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 268435399; // 1 gigabyte
//static constexpr size_t TP_TABLE_SIZE = 536870909; // 2 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 671088637; // 2.5 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 1073741827; // 4 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 1342177283; // 5 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 3221225533; // 12 gigabytes
//static constexpr size_t TP_TABLE_SIZE = 6710886419; // 25 gigabytes

using std::array;
using std::atomic;
using std::cerr;
using std::condition_variable;
using std::cout;
using std::endl;
using std::ifstream;
using std::lock_guard;
using std::mutex;
using std::ostream;
using std::string;
using std::stringstream;
using std::thread;
using std::unique_lock;
using std::vector;

#define RESULT_ABORTED (-100)

static mutex cout_mutex;

// number of internal ranks (i.e. those on which pawns can be
// without the game being over)
static constexpr int NUM_RANKS = N-2;

// number of internal squares
static constexpr int NUM_ISQ = N*NUM_RANKS;

// starting ranks
static constexpr int RANK_WHITE = 0;
static constexpr int RANK_BLACK = NUM_RANKS-1;

// a pawn must be in one of these ranks to even be candidate for en passant
static constexpr int EP_RANK_WHITE = RANK_WHITE+2;
static constexpr int EP_RANK_BLACK = RANK_BLACK-2;

// tighter limit? N*2 is not enough
static constexpr int MAX_LEGAL_MOVES = N*3;

// compactly represented position
typedef uint64_t pos_t;

class Timer {
    time_t start;
public:
    Timer() { start = time(NULL); }
    unsigned long elapsed() const { return time(NULL)-start; }
} timer;

static ostream &operator<<(ostream &str, const Timer &t) {
    return str << "[" << t.elapsed() << "]";
}

static const int flip_horiz_sq(int sq) {
    int rank = sq/N, file = sq%N;
    return rank*N + (N-1-file);
}

static const char *player_name(int player) {
    if (player == 1)
	return "White";
    else if (player == -1)
	return "Black";
    else
	abort();
}

static void assert_valid_sq(int x, int y) {
    assert(x >= 0);
    assert(x < N);
    assert(y >= 0);
    assert(y < N-2);
}

static const int SQ(int x, int y) {
    assert_valid_sq(x, y);
    return y*N+x;
}

static string sqname(int x, int y) {
    assert_valid_sq(x, y);
    stringstream sb;
    assert(N < 26);
    sb << (char)('a'+x);
    sb << 2+y;
    return sb.str();
}

static string sqname(int sq) {
    assert(sq >= 0);
    assert(sq < NUM_ISQ);
    return sqname(sq%N, sq/N);
}

// Singleton
class Compact_tab {
private:
    static Compact_tab *instance;
    static constexpr int SIZE = (N+1)*(N+1);
    array<uint64_t, SIZE> tab;
public:
    Compact_tab();
    uint64_t operator[](int n) const {
	assert(n >= 0);
	assert(n < SIZE);
	return tab[n];
    }
    uint64_t base(int nwhite, int nblack) const {
	assert(nwhite >= 0 && nwhite <= N);
	assert(nblack >= 0 && nblack <= N);
	assert(nwhite != 0 || nblack != 0);
	return tab[nwhite*(N+1)+nblack-1];
    }
    int num_white(int idx) const { return (idx+1)/(N+1); }
    int num_black(int idx) const { return (idx+1)%(N+1); }
    // returns the index of the last element <= n
    int find(uint64_t n) const {
	return std::upper_bound(&tab[0], &tab[SIZE], n) - tab.begin() - 1;
    }
} ranks_tab;

Compact_tab *Compact_tab::instance = nullptr;

Compact_tab::Compact_tab() {
    instance = this;

    init_binom();

    int p = 0;
    tab[p++] = 0;
    for (int white=0; white<=N; white++)
	for (int black=0; black<=N; black++) {
	    if (white == 0 && black == 0)
		continue;
	    // cout << "[" << p-1 << "]: " << white << "+" << black << ": "
	    // 	 << binom(NUM_ISQ, white) * binom(NUM_ISQ, black) * 2 << endl;
	    tab[p] = tab[p-1] + binom(NUM_ISQ, white) * binom(NUM_ISQ, black) * 2 * (N+1);
	    p++;
	}
    assert(p == SIZE);
    assert(tab[p-1] >> 62 == 0);
}

class Pos {
    array<int, NUM_ISQ> sq; // 1 = white, -1 = black, 0 = empty
    int turn; // 1 = white, -1 = black
    mutable int num_white = -1, num_black = -1; // calculated if/when needed
    int canonized_player_flip; // -1 changed player in canonize, else 1
    bool horiz_flipped;
    int ep_file; // en passant; -1 = no ep
    void force_count_pieces() const;
    void count_pieces() const { if (num_white == -1) force_count_pieces(); }
    void clear();
    // check if a hypothetical pawn at a square is unstoppable
    bool is_unstoppable(int sq) const;
public:
    struct Move {
	// 'replacing' = the contents of the square moved to
	// (so this information is enough to undo the move).
	int from, to, replacing, value;
	int new_ep_file, old_ep_file, ep_square;

	// Move(int from, int to, int replacing, int value, int ep_square=-1)
	//     : from(from), to(to), replacing(replacing), value(value), ep_square(ep_square)
	// {}

	Move() : from(-1), to(-1), replacing(-100), value(-9999999), new_ep_file(-1),
		 old_ep_file(-9999999), ep_square(-1) {}

	// used to remove symmetric moves if Pos::is_horiz_symmetric() returns true
	bool is_from_right_half() const { return from >= (N+1)/2; }

	Move decanonize(bool black, bool flip_horiz) const {
	    Move m(*this);
	    int from = this->from, to = this->to, replacing = this->replacing;
	    int old_ep_f = old_ep_file, new_ep_f = new_ep_file, ep_sq = ep_square;
	    if (black) {
		from = NUM_ISQ-1-from;
		to = NUM_ISQ-1-to;
		if (old_ep_f != -1)
		    old_ep_f = N-1-old_ep_f;
		if (new_ep_f != -1)
		    new_ep_f = N-1-new_ep_f;
		if (ep_sq != -1)
		    ep_sq = NUM_ISQ-1-ep_sq;
		replacing = -replacing;
	    }
	    if (flip_horiz) {
		from = flip_horiz_sq(from);
		to = flip_horiz_sq(to);
		if (old_ep_f != -1)
		    old_ep_f = N-1-old_ep_f;
		if (new_ep_f != -1)
		    new_ep_f = N-1-new_ep_f;
		if (ep_sq != -1)
		    ep_sq = flip_horiz_sq(ep_sq);
	    }
	    m.from = from;
	    m.to = to;
	    m.replacing = replacing;
	    m.old_ep_file = old_ep_f;
	    m.new_ep_file = new_ep_f;
	    m.ep_square = ep_sq;
	    return m;
	}

	string name() const {
	    int from_file = from%N, from_rank = from/N,
		to_file = to%N, to_rank = to/N;
	    assert(from_file >= 0);
	    assert(from_file < N);
	    assert(from_rank >= 0);
	    assert(from_rank < NUM_RANKS);

	    stringstream ss;
	    ss << ('a'+from_file) << ('1'+from_rank);
	    if (replacing)
		ss << 'x';
	    else if (ep_square != -1)
		ss << "(ep)";
	    ss << ('a'+to_file) << ('1'+to_rank);
	    return ss.str();
	}
    };

    Pos(); // initial position
    Pos(pos_t);
    pos_t pack() const;
    void check_sanity();
    ostream &print(ostream &str) const;
    void random_position();
    void random_position(int nwhites, int nblacks);
    int get_turn() const { return turn*canonized_player_flip; }
    void canonize();
    void horiz_mirror_board();
    bool is_horiz_symmetric() const;

    int winner() const; // -1 if won by black, 1 if by white, 0 otherwise
    int get_canonize_flip() const { return canonized_player_flip; }
    bool get_horiz_flipped() const { return horiz_flipped; }

    // Returns count.
    int get_legal_moves(array<Move, MAX_LEGAL_MOVES> &moves) const;

    void do_move(const Move &move);
    void undo_move(const Move &move);

    bool operator==(const Pos &) const;
};

// check if a hypothetical pawn at a square is unstoppable
bool Pos::is_unstoppable(int s) const {
    int file = s%N;
    if (file != 0 && file != N-1) {
	if (turn == 1) {
	    for (int s2=s+N; s2<NUM_ISQ; s2+=N)
		if (sq[s2-1] != 0 || sq[s2] != 0 || sq[s2+1] != 0)
		    return false;
	} else
	    for (int s2=s-N; s2>=0; s2-=N)
		if (sq[s2-1] != 0 || sq[s2] != 0 || sq[s2+1] != 0)
		    return false;
    } else {
	int othersq;
	if (file == 0)
	    othersq = 1;
	else
	    othersq = -1;
	if (turn == 1) {
	    for (int s2=s+N; s2<NUM_ISQ; s2+=N)
		if (sq[s2] != 0 || sq[s2+othersq] != 0)
		    return false;
	} else
	    for (int s2=s-N; s2>=0; s2-=N)
		if (sq[s2] != 0 || sq[s2+othersq] != 0)
		    return false;
    }
    return true;
}

bool Pos::is_horiz_symmetric() const {
    int left = 0, right = N-1;

    while (left < right) {
	for (int i=0; i<NUM_ISQ; i+=N)
	    if (sq[left+i] != sq[right+i])
		return false;
	left++;
	right--;
    }
    return true;
}

void Pos::horiz_mirror_board() {
    int left = 0, right = N-1;

    while (left < right) {
	for (int i=0; i<NUM_ISQ; i+=N) {
	    int tmp = sq[left+i];
	    sq[left+i] = sq[right+i];
	    sq[right+i] = tmp;
	}
	left++;
	right--;
    }

    if (ep_file != -1)
	ep_file = N-1-ep_file;

    horiz_flipped = !horiz_flipped;
    //check_sanity();
}

void Pos::canonize() {
    // Canonize so that white is always to move
    if (turn == -1) {
	turn = 1;
	canonized_player_flip = -canonized_player_flip;
	for (int from=0, to=NUM_ISQ-1; from < to; from++, to--) {
	    int tmp = sq[from];
	    sq[from] = -sq[to];
	    sq[to] = -tmp;
	}
	if (NUM_ISQ%2 == 1)
	    sq[NUM_ISQ/2] = -sq[NUM_ISQ/2];
	int tmp = num_white;
	num_white = num_black;
	num_black = tmp;
	if (ep_file != -1)
	    ep_file = N-1-ep_file;
	//check_sanity();
    }

    // Now possibly mirror the board horizontally
    bool horiz_done = false;
    for (int y=0; y<NUM_RANKS && !horiz_done; y++) {
	int left = SQ(0,y);
	int right = left+N-1;
	while (left < right) {
	    if (sq[left] < sq[right]) {
		horiz_mirror_board();
		horiz_done = true;
		break;
	    } else if (sq[left] > sq[right]) {
		horiz_done = true;
		break;
	    } else {
		left++;
		right--;
	    }
	}
    }
}

ostream &operator<<(ostream &os, const Pos::Move &move) {
    os << sqname(move.from);
    if (move.replacing)
	os << "x";
    os << sqname(move.to);
    return os;
}

void Pos::do_move(const Move &move) {
    assert(num_white >= 0 && num_black >= 0);

    assert(sq[move.from] == turn);
    assert(sq[move.to] == move.replacing);
    sq[move.from] = 0;
    sq[move.to] = turn;

    bool captured = false;

    if (move.replacing) {
	assert(move.replacing == -turn);
	captured = true;
    } else if (move.ep_square != -1) {
	assert(sq[move.ep_square] == -turn);
	sq[move.ep_square] = 0;
	captured = true;
    }

    if (captured) {
	if (turn == 1)
	    num_black--;
	else
	    num_white--;
    }

    assert(ep_file == move.old_ep_file);
    ep_file = move.new_ep_file;

    turn = -turn;
}

void Pos::undo_move(const Move &move) {
    turn = -turn;
    assert(sq[move.to] == turn);
    assert(sq[move.from] == 0);
    sq[move.from] = turn;
    sq[move.to] = move.replacing;

    bool captured = false;

    if (move.replacing) {
	assert(move.replacing == -turn);
	captured = true;
    } else if (move.ep_square != -1) {
	assert(sq[move.ep_square] == 0);
	sq[move.ep_square] = -turn;
	captured = true;
    }

    if (captured) {
	if (turn == 1)
	    num_black++;
	else
	    num_white++;
    }

    assert(ep_file == move.new_ep_file);
    ep_file = move.old_ep_file;
}

int Pos::get_legal_moves(array<Pos::Move, MAX_LEGAL_MOVES> &moves) const {
    array<int, N> positions;
    int num_pawns = 0, num_moves = 0;

    //print(cerr);

    if (winner() != 0)
	return 0;

    // for (int i=0; i<MAX_LEGAL_MOVES; i++)
    // 	moves[i].value = 999;

    // try to return potentially more useful moves first
    if (turn == -1) {
	for (int i=0; i<NUM_ISQ; i++)
	    if (sq[i] == turn)
		positions[num_pawns++] = i;
    } else {
	for (int i=NUM_ISQ-1; i>=0; i--)
	    if (sq[i] == turn)
		positions[num_pawns++] = i;
    }

    assert(num_pawns <= N);

    // evaluation function: sum of ranks of pawns squared +
    // 100*(2+rank) for best unstoppable pawn

    int best_unstoppable = -1, best_unstoppable_rank = -1;

    for (int i=0; i<num_pawns; i++) {
	int s = positions[i], file = s%N;
	int file_centrality = std::min(file, N-1-file);
	int front = s + turn*N; // sq in front of current
	int rank = s/N;
	if (turn == -1)
	    rank = RANK_BLACK-rank;
	if (sq[front] == 0) {
	    // front square empty, add it
	    moves[num_moves].from = s;
	    moves[num_moves].to = front;
	    moves[num_moves].value = rank+file_centrality;
	    if (rank+1 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
		best_unstoppable = num_moves;
		best_unstoppable_rank = rank+1;
	    }
	    moves[num_moves++].replacing = 0;
	    if (N >= 5 && rank == 0) {
		// move ahead 2 squares?
		int front2 = front + turn*N;
		assert(front2 >= 0);
		assert(front2 < NUM_ISQ);
		if (sq[front2] == 0) {
		    moves[num_moves].from = s;
		    moves[num_moves].to = front2;
		    moves[num_moves].value = rank+2+file_centrality;
		    if (rank+2 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
			best_unstoppable = num_moves;
			best_unstoppable_rank = rank+2;
		    }
		    // if there's something that can capture this, mark file as en passant
		    if ((file != 0 && sq[front2-1] == -turn) ||
			(file != N-1 && sq[front2+1] == -turn))
			moves[num_moves].new_ep_file = file;
		    moves[num_moves++].replacing = 0;
		}
	    }
	}
	if (file != 0 && sq[front-1] == -turn) {
	    // may capture to left
	    moves[num_moves].from = s;
	    moves[num_moves].to = front-1;
	    moves[num_moves].value = rank+file_centrality;
	    moves[num_moves].value += (NUM_RANKS-rank)*(NUM_RANKS-rank) + 1;
	    if (rank+1 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
		best_unstoppable = num_moves;
		best_unstoppable_rank = rank+1;
	    }
	    moves[num_moves++].replacing = -turn;
	}
	if (file != N-1 && sq[front+1] == -turn) {
	    // may capture to right
	    moves[num_moves].from = s;
	    moves[num_moves].to = front+1;
	    moves[num_moves].value = rank+file_centrality;
	    moves[num_moves].value += (NUM_RANKS-rank)*(NUM_RANKS-rank) + 1;
	    if (rank+1 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
		best_unstoppable = num_moves;
		best_unstoppable_rank = rank+1;
	    }
	    moves[num_moves++].replacing = -turn;
	}
	if ((turn == 1 && rank == EP_RANK_BLACK) || (turn == -1 && rank == EP_RANK_WHITE)) {
	    if (file != 0 && ep_file == file-1) {
		// may capture en passant to left
		assert(sq[s-1] == -turn);
		assert(sq[front-1] == 0);
		moves[num_moves].from = s;
		moves[num_moves].to = front-1;
		moves[num_moves].value = rank+file_centrality;
		moves[num_moves].value += (NUM_RANKS-rank+1)*(NUM_RANKS-rank+1)+1;
		if (rank+1 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
		    best_unstoppable = num_moves;
		    best_unstoppable_rank = rank+1;
		}
		assert(sq[moves[num_moves].to] == 0);
		moves[num_moves].ep_square = s-1;
		moves[num_moves++].replacing = 0;
	    }
	    if (file != N-1 && ep_file == file+1) {
		// may capture en passant to right
		assert(sq[s+1] == -turn);
		assert(sq[front+1] == 0);
		moves[num_moves].from = s;
		moves[num_moves].to = front+1;
		moves[num_moves].value = rank+file_centrality;
		moves[num_moves].value += (NUM_RANKS-rank+1)*(NUM_RANKS-rank+1)+1;
		if (rank+1 > best_unstoppable_rank && is_unstoppable(moves[num_moves].to)) {
		    best_unstoppable = num_moves;
		    best_unstoppable_rank = rank+1;
		}
		moves[num_moves].ep_square = s+1;
		moves[num_moves++].replacing = 0;
	    }
	}
    }

    assert(num_moves <= MAX_LEGAL_MOVES);

    if (best_unstoppable != -1)
	moves[best_unstoppable].value += 100*(2+best_unstoppable_rank);

    for (Move &m : moves)
	m.old_ep_file = ep_file;

    for (int i=0; i<num_moves-1; i++)
    	for (int j=i+1; j<num_moves; j++) {
	    //assert(moves[i].value != 999);
	    //assert(moves[j].value != 999);
    	    if (moves[j].value > moves[i].value) {
    		Move tmp = moves[i];
    		moves[i] = moves[j];
    		moves[j] = tmp;
    	    }
    	}

    return num_moves;
}

int Pos::winner() const {
    int base;

    assert(num_white >= 0 && num_black >= 0);

    if (num_white == 0) {
	assert(num_black > 0);
	return -1; /* *canonized_player_flip;*/
    } else if (num_black == 0)
	return 1; /* canonized_player_flip; */

    if (turn == 1)
	base = SQ(0, NUM_RANKS-1);
    else {
	assert(turn == -1);
	base = SQ(0, 0);
    }
    for (int i=0; i<N; i++)
	if (sq[base+i] == turn)
	    return turn; /**canonized_player_flip;*/
    return 0;
}

bool Pos::operator==(const Pos &a) const {
    if (turn != a.turn)
	return false;

    for (int i=0; i<NUM_ISQ; i++)
	if (sq[i] != a.sq[i])
	    return false;

    if (ep_file != a.ep_file)
	return false;

    return true;
}

void Pos::force_count_pieces() const {
    num_white = num_black = 0;
    for (int i=0; i<NUM_ISQ; i++)
	if (sq[i] == -1)
	    num_black++;
	else if (sq[i] == 1)
	    num_white++;
	else
	    assert(sq[i] == 0);

    if (num_white > N) {
	cerr << "White has " << num_white << " pawns (>N)!" << endl;
	this->print(cerr);
	abort();
    }
    else if (num_black > N) {
	cerr << "Black has" << num_black << " pawns (>N)!" << endl;
	this->print(cerr);
	abort();
    }
}

Pos::Pos()
    : turn(1)
    , canonized_player_flip(1)
    , horiz_flipped(false)
    , ep_file(-1)
{
    for (int i=0; i<NUM_ISQ; i++)
	sq[i] = 0;

    for (int i=0; i<N; i++) {
	sq[SQ(i, RANK_WHITE)] = 1;
	sq[SQ(i, RANK_BLACK)] = -1;
    }

    count_pieces();
}

pos_t Pos::pack() const {
    count_pieces();
    uint64_t base = ranks_tab.base(num_white, num_black);
    array<array<int, NUM_ISQ>, 3> squares; // black, empty, white
    array<int, 3> num_squares{{0}};

    for (int i=0; i<NUM_ISQ; i++) {
	//assert(sq[i] >= -1 && sq[i] <= 1);
	squares[sq[i]+1][num_squares[sq[i]+1]++] = i;
    }
    int num_white = num_squares[2], num_black = num_squares[0];

    uint64_t whites_rank = rank_combination(squares[2].data(), num_white);
    uint64_t blacks_rank = rank_combination(squares[0].data(), num_black);

    uint64_t offset = whites_rank;
    offset = offset * binom(NUM_ISQ, num_black) + blacks_rank;
    offset = offset * 2 + (turn == -1);
    offset = offset * (N+1) + ep_file + 1;

    bool error = false;

    if (DEBUG) {
	if (num_white != N || num_black != N) {
	    uint64_t base_range = ranks_tab[ranks_tab.find(base)+1]-ranks_tab[ranks_tab.find(base)];
	    if (offset >= base_range) {
		cerr << "pack error: offset >= base_range." << endl;
		error = true;
	    }
	}
    }

    if (ranks_tab.find(base + offset) != num_white*(N+1)+num_black-1) {
	cerr << "pack error: wrong index" << endl;
	error = true;
    }

    if (error) {
	print(cerr);
	cerr << "base = " << base << endl;
	cerr << "offset = " << offset << endl;
	cerr << "whites_rank = " << whites_rank << endl;
	cerr << "blacks_rank = " << blacks_rank << endl;
	cerr << "num_white*(N+1)+num_black = " << num_white*(N+1)+num_black << endl;
	cerr << "ranks_tab.find(base) = " << ranks_tab.find(base) << endl;
	cerr << "ranks_tab@base = " << ranks_tab[ranks_tab.find(base)] << endl;
	cerr << "base_range = " << ranks_tab[ranks_tab.find(base)+1]-ranks_tab[ranks_tab.find(base)] << endl;
	cerr << "ranks_tab.find(base + offset) = index " << ranks_tab.find(base+offset) << endl;
	cerr << "ranks_tab.find(base + offset) = " << ranks_tab[ranks_tab.find(base+offset)] << endl;
	abort();
    }
    return base + offset;
}

Pos::Pos(pos_t compact) {
    clear();

    uint64_t idx = ranks_tab.find(compact);
    assert(ranks_tab[idx] <= compact);
    uint64_t base = ranks_tab[idx];
    uint64_t offset = compact-base;

    num_black = (idx+1)%(N+1);
    num_white = (idx+1)/(N+1);

    ep_file = (offset % (N+1)) - 1;
    offset /= N+1;

    if (offset % 2)
	turn = -1;
    else
	turn = 1;
    offset /= 2;

    uint64_t b = binom(NUM_ISQ, num_black);
    uint64_t blacks_rank = offset%b;
    uint64_t whites_rank = offset/b;

    array<int, N> squares;
    unrank_combination(squares.data(), num_white, whites_rank);
    for (int i=0; i<num_white; i++) {
	assert(squares[i] >= 0);
	assert(squares[i] < NUM_ISQ);
	sq[squares[i]] = 1;
    }

    unrank_combination(squares.data(), num_black, blacks_rank);
    for (int i=0; i<num_black; i++) {
	assert(squares[i] >= 0);
	assert(squares[i] < NUM_ISQ);
	sq[squares[i]] = -1;
    }

}


// mainly check that no player has more than N pawns
void Pos::check_sanity() {
    assert(turn == -1 || turn == 1);
    assert(num_white >= 0 && num_black >= 0);

    int s = 0;
    for (int i=0; i<NUM_ISQ; i++) {
	if (sq[i] == 1)
	    s += i/N;
	else if (sq[i] == -1)
	    s += (NUM_RANKS-1-i/N);
    }
}

ostream &Pos::print(ostream &str) const {
    array<char, N*2+2> delim;

    for (int i=0; i<N; i++) {
	delim[i*2] = '+';
	delim[i*2+1] = '-';
    }
    delim[N*2] = '+';
    delim[N*2+1] = 0;

    for (int y=N-1; y >= 0; y--) {
	str << delim.data() << "\n";
	str << "|";
	for (int x=0; x < N; x++) {
	    if (y == 0 || y == N-1)
		str << " ";
	    else if (y > 0 && y < N-1) {
		int t = sq[SQ(x, y-1)];
		assert(t == 0 || t == -1 || t == 1);
		str << "o x"[t+1];
	    }
	    str << "|";
	}
	if (y == 0)
	    str << "   " << player_name(turn) << " to move";
	str << "\n";
    }
    str << delim.data() << "\n";
    return str;
}

void Pos::clear() {
    for (int i=0; i<NUM_ISQ; i++)
	sq[i] = 0;
    num_white = num_black = 0;
}

void Pos::random_position() {
    int w = 0, b = 0;
    do {
	int r = rand();
	w = r % N;
	r /= N;
	b = r%N;
    } while (w == 0 && b == 0);
    random_position(w, b);
}

void Pos::random_position(int nw, int nb) {
    assert(nw >= 0 && nw <= N);
    assert(nb >= 0 && nb <= N);
    assert(nw != 0 || nb != 0);
    clear();
    num_white = nw;
    num_black = nb;
    while (nw) {
	int x = rand()%NUM_ISQ;
	if (sq[x] == 0) {
	    sq[x] = 1;
	    nw--;
	}
    }

    while (nb) {
	int x = rand()%NUM_ISQ;
	if (sq[x] == 0) {
	    sq[x] = -1;
	    nb--;
	}
    }

    if (rand() % 2)
	turn = -1;
    else
	turn = 1;

    // now check if any of the pawns not in turn could be just moved
    // two spaces, and possibly mark one of them as en passant
    const int prev_turn = -turn;
    const bool prev_was_white = (turn == -1);
    const int ep_rank = prev_was_white ? EP_RANK_WHITE : EP_RANK_BLACK;
    const int first_ep_square = SQ(0, ep_rank);

    ep_file = -1;

    int ep_files[N], ep_count=0;
    int ep_backward = turn*N;
    for (int i=0; i<N; i++) {
	if (sq[first_ep_square+i] == prev_turn) {
	    if (((i != 0 && sq[first_ep_square+i-1] == turn) ||
		 (i != N-1 && sq[first_ep_square+i+1] == turn)) &&
		sq[first_ep_square+i+ep_backward] == 0 &&
		sq[first_ep_square+i+2*ep_backward] == 0)
		ep_files[ep_count++] = i; // something can take this
	}
    }

    if (ep_count == 0)
	return;

    int ep = rand() % (ep_count + 1);
    if (ep == ep_count)
	ep_file = -1; // no en passant this time
    else
	ep_file = ep_files[ep];
}

void count_boards() {
    uint64_t total = 0;
    cout << "Possible " << N << "x" << N << " boards with a+b pawns:" << endl;
    for (int a=1; a<=N; a++)
	for (int b=1; b<=N; b++) {
	    uint64_t count = binom(NUM_ISQ, a) * binom(NUM_ISQ, b);
	    total += count;
	    cout << a << "+" << b << "\t"
		 << log(count)/log(2) << "\t\t" << count << endl;
	}
    cout << "\ntotal\t" << log(total)/log(2) << "\t\t" << total << endl;
}

void test_pack_unpack() {
    int i = 0;
    while (true) {
	Pos p;
	p.random_position();
	uint64_t packed = p.pack();
	//p.print(cout);
	Pos p2(packed);
	// cout << "p:\n";
	// p.print(cout);
	// cout << "p2:\n";
	// p.print(cout);
	assert(p == p2);
	//cout << "--------------------\n";
	//cout << p.pack() << endl;
	if (++i % 10000 == 0)
	    cout << i << endl;
    }
}

void test_do_undo_move() {
    pos_t position_number;
    int count = 0;
    int verbose = 0;
    while (true) {
	Pos p;
	p.random_position();
	Pos origpos(p);
	array<Pos::Move, MAX_LEGAL_MOVES> moves;
	position_number = p.pack();
	// if (position_number == 20137260669466283LL)
	//     verbose=1;
	if (verbose) {
	    cout << "Getting legal moves for position " << position_number << ":" << endl;
	    p.print(cout);
	}
	int num_moves = p.get_legal_moves(moves);
	if (num_moves == 0)
	    continue;
	if (verbose)
	    p.print(cout);
	for (int i=0; i<num_moves; i++) {
	    if (verbose)
		cout << "Testing do-undo " << moves[i] << "..." << endl;
	    p.do_move(moves[i]);
	    //cout << "After move: " << endl;
	    //p.print(cout);
	    p.undo_move(moves[i]);
	    if (!(p == origpos)) {
		cout << "Do-undo-move altered position! Original ("
		     << origpos.pack() << ")" << endl;
		origpos.print(cout);
		cout << "After do-undo move " << moves[i] << " ("
		     << p.pack() << "):" << endl;
		p.print(cout);
		abort();
	    }
	}
	if (++count % 100000 == 0)
	    cout << count << endl;
    }
}

MemTranspositionTable<TP_TABLE_SIZE> tp_table;
//CachedTranspositionTable<MemTranspositionTable<30146531>, MemTranspositionTable<TP_TABLE_SIZE> > tp_table;

// static void save_table() {
//     stringstream fname;
//     fname << "tp_" << N << "_" << TP_TABLE_SIZE << ".data";
//     tp_table.save(fname.str().c_str());
// }

// static void load_table() {
//     stringstream fname;
//     fname << "tp_" << N << "_" << TP_TABLE_SIZE << ".data";
//     {
// 	ifstream f(fname.str());
// 	if (!f) {
// 	    cout << "No transposition table to load." << endl;
// 	    return;
// 	}
//     }
//     cout << "Loading transposition table from " << fname.str() << "..." << endl;
//     tp_table.load(fname.str().c_str());
//     cout << "Done." << endl;
// }

// static void handle_signal(int signal) {
//     assert(signal == SIGHUP || signal == SIGINT);

//     cout << "Signal received, saving transposition table..." << endl;
//     save_table();
//     cout << "Done." << endl;

//     if (signal == SIGINT)
// 	exit(0);
// }

struct DepthInfo {
    int curr_move_num;
    int num_moves;
    Pos::Move move;
    int alpha, beta;
};

typedef array<DepthInfo, VERBOSE_DEPTH> DepthInfoArray;

static atomic<uint64_t> node_count{0};
//static uint64_t node_count{0};

static int negamax(Pos &p, int depth, int alpha, int beta, pos_t packed,
		   DepthInfoArray &depth_info);

static int try_move(Pos &p, const Pos::Move &move, int depth, int alpha, int beta,
		    DepthInfoArray &depth_info) {
    assert(alpha < beta);

    const int turn = p.get_turn();

    //cout << "Taking move " << moves[i] << endl;
    //p->check_sanity();
    p.do_move(move);

    Pos canonized(p);
    // cerr << "Before canonize:" << endl;
    // canonized.print(cerr);
    canonized.canonize();
    assert(canonized.get_turn() == -turn);
    // cerr << "After canonize:" << endl;
    // canonized.print(cerr);
    // cerr << "------------------------------------------------------------" << endl;

    pos_t packed = 0;
    bool got_result = false;
    int result = 0;

    packed = canonized.pack();
    //assert(packed%2 == 0);
    //packed /= 2;
    TpResult tpResult = tp_table.probe(packed);
    // if (turn == -1)
    //tpResult = flip_result(tpResult);

    switch(tpResult) {
    case TpResult::NONE:
	break;
    case TpResult::CURRENT_LOSS:
	result = -1;
	got_result = true;
	break;
    case TpResult::DRAW:
	result = 0;
	got_result = true;
	break;
    case TpResult::CURRENT_WIN:
	result = 1;
	got_result = true;
	break;
    case TpResult::LOWER_BOUND_0:
	if (-beta < 0) {
	    beta = 0;
	    if (-alpha <= 0) {
		result = 0;
		got_result = true;
	    }
	}
	break;
    case TpResult::UPPER_BOUND_0:
	if (-alpha > 0) {
	    alpha = 0;
	    if (-beta >= 0) {
		result = 0;
		got_result = true;
	    }
	}
    }

    //p->check_sanity();

    if (!got_result) {
	//uint64_t saved_node_count = node_count;
	result = negamax(canonized, depth+1, -beta, -alpha, packed, depth_info);
    }

    //cout << "Undoing move " << move << endl;
    p.undo_move(move);
    //p.check_sanity();
    if (result == RESULT_ABORTED)
	return RESULT_ABORTED;
    return -result;
}

static int try_move_copy(Pos p, const Pos::Move &move, int depth, int alpha, int beta,
			 DepthInfoArray &depth_info) {
    return try_move(p, move, depth, alpha, beta, depth_info);
}

static void report_depthinfo(int depth, const DepthInfoArray &depth_info, int alpha,
			     int beta, int result) {
    const double size = tp_table.size()/double(TP_TABLE_SIZE)*100.0;
    const bool white_to_move = (depth%2 == 1);

    if (!white_to_move) {
	int old_beta = beta;
	beta = -alpha;
	alpha = -old_beta;
	result = -result;
    }

    {
	//lock_guard<mutex> guard(cout_mutex);
	cout << timer << "\t";
	for (int j=0; j<depth; j++) {
	    cout << depth_info[j].curr_move_num << "/"
		 << depth_info[j].num_moves;
	    int alpha = depth_info[j].alpha, beta = depth_info[j].beta;
	    assert(alpha >= -1);
	    assert(alpha <= 1);
	    assert(beta >= -1);
	    assert(beta <= 1);
	    assert(alpha < beta);
	    if (j%2 == 1) {
		int old_beta = beta;
		beta = -alpha;
		alpha = -old_beta;
	    }
	    if (alpha == 0)
		cout << "-";
	    else if (beta == 0)
		cout << "+";
	    cout << "\t";
	}
	for (int j=depth; j<VERBOSE_DEPTH; j++)
	    cout << "\t";

	//cout << "<" << std::setw(2) << alpha << "," << beta << ">\t";

	cout.setf(std::ios::fixed, std::ios::floatfield);
	cout << size << "%" << "\t";

	for (int j=0; j<depth; j++) {
	    if (j%2 == 0)
		cout << j/2+1 << ". ";
	    cout << depth_info[j].move;
	    cout << " ";
	}

	switch(result) {
	case -1:
	    cout << "0-1";
	    break;
	case 0:
	    cout << "1/2-1/2";
	    if (alpha == 0)
		cout << "-";
	    else if (beta == 0)
		cout << "+";
	    break;
	case 1:
	    cout << "1-0";
	    break;
	default:
	    assert(false);
	    abort();
	}
	cout << endl;
	// cout << "Depth " << depth << ": move "
	// 	<< i+1 << "/" << num_moves << "... " << endl;
    }
}

static atomic<bool> abortRequested{false};
static bool threads_running = false;
static mutex threads_free_mutex;
static condition_variable threads_free_cond;
static int threads_free_count = NUM_THREADS;

class ThreadFreer {
    bool parallelize;
public:
    ThreadFreer(bool parallelize) : parallelize(parallelize) {}
    ~ThreadFreer() {
	if (parallelize) {
	    {
		unique_lock<mutex> guard(threads_free_mutex);
		++threads_free_count;
	    }
	    threads_free_cond.notify_one();
	}
    }
};

static int negamax(Pos &p, int depth, int alpha, int beta, pos_t packed,
		   DepthInfoArray &depth_info) {
    if (DEBUG_POSITION != 0 && packed == DEBUG_POSITION) {
	cout << "negamax: start " << packed << ", ab=" << alpha << "," << beta << endl;
	p.print(cout);
    }

    if (abortRequested.load(std::memory_order_relaxed)) {
	assert(threads_running);
	return RESULT_ABORTED;
    }
    node_count.fetch_add(1, std::memory_order_relaxed);

    array<Pos::Move, MAX_LEGAL_MOVES> moves;
    //array<int, MAX_LEGAL_MOVES> results;

    const int turn = p.get_turn();
    int num_legal_moves = p.get_legal_moves(moves);

    //p->print(cout);

    if (num_legal_moves == 0) {
	//cout << "Game over." << endl;
	return p.winner();
    }

    const bool is_horiz_symm = p.is_horiz_symmetric();
    if (is_horiz_symm) {
	int p = 0;
	for (int i=0; i<num_legal_moves; i++)
	    if (!moves[i].is_from_right_half())
		moves[p++] = moves[i];
	num_legal_moves = p;
    }

    if (depth <= VERBOSE_DEPTH)
	depth_info[depth-1].num_moves = num_legal_moves;

    const int alpha_orig = alpha;
    int best_value = -1;

    bool parallelize_rest = !threads_running && depth <= PARALLEL_DEPTH &&
	depth >= PARALLEL_MIN_DEPTH;
    bool parallelize = parallelize_rest && (alpha+beta != 0 || depth < CUT_MIN_DEPTH);
    std::array<int, MAX_LEGAL_MOVES> results;

    // alpha and beta won't change while this is executing, but they
    // haven't been set at this point.
    auto search_move = [&p, depth, &alpha, &beta, &parallelize, &moves, turn, &results,
			num_legal_moves](int i, DepthInfoArray &depth_info) {
	ThreadFreer freer(parallelize);
	int result;
	if (depth <= VERBOSE_DEPTH) {
	    depth_info[depth-1].curr_move_num = i+1;
	    depth_info[depth-1].move = moves[i].decanonize(p.get_turn() == -1,
							   p.get_horiz_flipped());
	    depth_info[depth-1].alpha = alpha;
	    depth_info[depth-1].beta = beta;
	}

	if (parallelize)
	    result = try_move_copy(p, moves[i], depth, alpha, beta, depth_info);
	else
	    result = try_move(p, moves[i], depth, alpha, beta, depth_info);
	results[i] = result;

	if (result == RESULT_ABORTED) {
	    assert(threads_running);
	    return;
	}

	if (depth <= VERBOSE_DEPTH) {
	    {
		lock_guard<mutex> guard(cout_mutex);
		//cout << "depth " << depth << ": result=" << result*turn << endl;
		report_depthinfo(depth, depth_info, alpha, beta, result);
	    }

	    if (depth == 1) {
		lock_guard<mutex> guard(cout_mutex);
		cout << timer << "\tDepth " << depth << ": move "
		     << i+1 << "/" << num_legal_moves << " RESULT=" << result*turn << endl;
		//canonized.print(cout);
		size_t a = tp_table.size();
		cout << timer << "\tTransposition table size = " << a << " ("
		     << a/double(TP_TABLE_SIZE)*100.0 << "% full)" << endl;
	    }
	}

	if (parallelize && depth >= CUT_MIN_DEPTH) {
	    // See if we can cut off
	    int new_alpha = std::max(result, alpha);
	    if (new_alpha >= beta)
		abortRequested.store(true, std::memory_order_relaxed);
	}
    };

    int next_move = 0;
    if (!parallelize) {
	for (int i=0; i<num_legal_moves; i++) {
	    assert(alpha < beta);
	    search_move(i, depth_info);
	    if (DEBUG_POSITION != 0 && packed == DEBUG_POSITION) {
		cout << "Move " << i << ": result=" << results[i] << endl;
	    }
	    if (results[i] == RESULT_ABORTED)
		return RESULT_ABORTED;
	    best_value = std::max(results[i], best_value);
	    if (depth >= CUT_MIN_DEPTH)
		alpha = std::max(results[i], alpha);
	    if (alpha >= beta)
		break; /* alpha cutoff */
	    if (parallelize_rest && alpha + beta != 0) {
		next_move = i+1;
		parallelize = true;
		break;
	    }

	}
    }

    if (parallelize) {
	assert(alpha < beta);
	// alpha and beta are guaranteed to not change here.
	vector<thread> threads;
	std::array<DepthInfoArray, MAX_LEGAL_MOVES> depth_infos;
	std::fill(depth_infos.begin(), depth_infos.end(), depth_info);

	threads_running = true;
	assert(alpha < beta);
	for (int i=next_move; i<num_legal_moves; i++) {
	    {
		results[i] = RESULT_ABORTED;
		unique_lock<mutex> guard(threads_free_mutex);
		while (threads_free_count == 0)
		    threads_free_cond.wait(guard);
		--threads_free_count;
	    }
	    threads.emplace_back(search_move, i, std::ref(depth_infos[i]));
	}

	for (auto &t : threads)
	    t.join();
	threads_running = false;

	abortRequested.store(false, std::memory_order_relaxed);

	for (int i=0; i<num_legal_moves; i++)
	    if (results[i] != RESULT_ABORTED)
		best_value = std::max(best_value, results[i]);
    }

    // if (alpha >= beta)
    // 	best_value = 0; // cutoff done

    if (depth > 1) { // packed is not valid for depth=1
	TpResult tp_res;
	if (best_value == -1) {
	    if (alpha_orig == 0)
		tp_res = TpResult::UPPER_BOUND_0;
	    else {
		assert(alpha_orig == -1);
		tp_res = TpResult::CURRENT_LOSS;
	    }
	} else if (best_value == 1) {
	    if (beta == 0)
		tp_res = TpResult::LOWER_BOUND_0;
	    else {
		assert(beta == 1);
		tp_res = TpResult::CURRENT_WIN;
	    }
	} else {
	    assert(best_value == 0);
	    if (alpha_orig == 0)
		tp_res = TpResult::UPPER_BOUND_0;
	    else if (beta == 0)
		tp_res = TpResult::LOWER_BOUND_0;
	    else {
		assert(alpha_orig == -1 && beta == 1);
		tp_res = TpResult::DRAW;
	    }
	}
	// if (turn == -1)
	//     tp_res = flip_result(tp_res);
	tp_table.add(packed, tp_res);
    }
    assert(best_value >= -1);
    assert(best_value <= 1);
    return best_value;
}

int main() {
    //struct sigaction sa;

    // FIXME signals and threads don't mix
    //sa.sa_handler = handle_signal;
    //sa.sa_flags = SA_RESTART;

    //sigaction(SIGHUP, &sa, nullptr);
    //sigaction(SIGINT, &sa, nullptr);
    //count_boards();
    //test_pack_unpack();
    //test_do_undo_move();
    //exit(0);

    //load_table();

    //map<pos_t, int> tp_table;
    Pos p;
    // array<Pos::Move, MAX_LEGAL_MOVES> m;
    // p.get_legal_moves(m);
    // p.do_move(m[6]);
    // p.canonize();

    DepthInfoArray depth_info;

    int result = negamax(p, 1, -1 /* alpha */, 1 /* beta */, 0 /* packed */,
			 depth_info);

    cout << timer << "\tresult=" << result << endl;
}
