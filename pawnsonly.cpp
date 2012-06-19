#include "MemTranspositionTable.hpp"
#include "binom.hpp"
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <vector>

#define DEBUG 1
#define VERBOSE_DEPTH 5

// # of 8-byte elements; try to choose a prime
static const size_t TP_TABLE_SIZE = 536870909; // 4 gigabytes
//static const size_t TP_TABLE_SIZE = 671088637; // 5 gigabytes
//static const size_t TP_TABLE_SIZE = 134217689; // 1 gigabyte
//static const size_t TP_TABLE_SIZE = 9614669;
//static const size_t TP_TABLE_SIZE = 30146531; // 230 megabytes

using std::ostream;
using std::cout;
using std::endl;
using std::cerr;
using std::string;
using std::stringstream;

// board size (number of pawns per side). Must be >= 4.
static const int N = 7;

// number of internal ranks (i.e. those on which pawns can be
// without the game being over)
static const int NUM_RANKS = N-2;

// number of internal squares 
static const int NUM_ISQ = N*NUM_RANKS;

// starting ranks
static const int RANK_WHITE = 0;
static const int RANK_BLACK = NUM_RANKS-1;

// tighter limit? N*2 is not enough
static const int MAX_LEGAL_MOVES = N*3;

// compactly represented position
typedef uint64_t pos_t;

const char *player_name(int player) {
    if (player == 1)
	return "White";
    else if (player == -1)
	return "Black";
    else
	assert(0);
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

// Singleton; 
class Compact_tab {
private:
    static Compact_tab *instance;
    static constexpr int SIZE = (N+1)*(N+1);
    uint64_t tab[SIZE];
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
	return std::upper_bound(tab, tab+SIZE, n) - tab - 1;
    }
} ranks_tab;

Compact_tab *Compact_tab::instance = nullptr;

Compact_tab::Compact_tab() {
    instance = this;

    init_binom();

    int p=0;
    tab[p++] = 0;
    for (int white=0; white<=N; white++)
	for (int black=0; black<=N; black++) {
	    if (white == 0 && black == 0)
		continue;
	    // cout << "[" << p-1 << "]: " << white << "+" << black << ": "
	    // 	 << binom(NUM_ISQ, white) * binom(NUM_ISQ, black) * 2 << endl;
	    tab[p] = tab[p-1] + binom(NUM_ISQ, white) * binom(NUM_ISQ, black) * 2;
	    p++;
	}
    assert(p == SIZE);
    assert(tab[p-1] >> 62 == 0);
}

class Pos {
    int sq[NUM_ISQ]; // 1 = white, -1 = black, 0 = empty
    int turn; // 1 = white, -1 = black
    mutable int num_white = -1, num_black = -1; // calculated if/when needed
    // FIXME en passant square
    void force_count_pieces() const;
    void count_pieces() const { if (num_white == -1) force_count_pieces(); }
    void clear();
public:
    // 'replacing' = the contents of the square moved to
    // (so this information is enough to undo the move)
    struct Move {
	int from, to, replacing, value;
    };

    Pos(); // initial position
    Pos(pos_t);
    pos_t pack() const;
    void check_sanity();
    ostream &print(ostream &str) const;
    void random_position();
    void random_position(int nwhites, int nblacks);
    int get_turn() const { return turn; }

    int winner() const; // -1 if won by black, 1 if by white, 0 otherwise

    // have space for MAX_LEGAL_MOVES. Returns count.
    int get_legal_moves(Move *moves) const;

    void do_move(const Move &move);
    void undo_move(const Move &move);

    bool operator==(const Pos &) const;
};

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
    if (move.replacing) {
	assert(move.replacing == -turn);
	if (turn == 1)
	    num_black--;
	else
	    num_white--;
    }
    turn = -turn;
}

void Pos::undo_move(const Move &move) {
    turn = -turn;
    assert(sq[move.to] == turn);
    assert(sq[move.from] == 0);
    sq[move.from] = turn;
    sq[move.to] = move.replacing;
    if (move.replacing) {
	assert(move.replacing == -turn);
	if (turn == 1)
	    num_black++;
	else
	    num_white++;
    }
}

int Pos::get_legal_moves(Pos::Move *moves) const {
    int positions[N], num_pawns = 0, num_moves = 0;

    if (winner() != 0)
	return 0;

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

    // evaluation function: sum of ranks of pawns squared

    for (int i=0; i<num_pawns; i++) {
	int s = positions[i], file = s%N;
	int front = s + turn*N; // sq in front of current
	int rank = s/N;
	if (turn == -1)
	    rank = NUM_RANKS-1-rank;
	if (sq[front] == 0) {
	    // front square empty, add it
	    moves[num_moves].from = s;
	    moves[num_moves].to = front;
	    moves[num_moves].value = 2*rank+1;
	    moves[num_moves++].replacing = 0;
	    if (N >= 5 && rank == 0) {
		// move ahead 2 squares?
		int front2 = front + turn*N;
		assert(front2 >= 0);
		assert(front2 < NUM_ISQ);
		if (sq[front2] == 0) {
		    moves[num_moves].from = s;
		    moves[num_moves].to = front2;
		    moves[num_moves].value = 4*rank+4;
		    moves[num_moves++].replacing = 0;
		}
	    }
	}
	if (file != 0 && sq[front-1] == -turn) {
	    // may capture to left
	    moves[num_moves].from = s;
	    moves[num_moves].to = front-1;
	    moves[num_moves].value = 2*rank+1;
	    moves[num_moves].value += (NUM_RANKS-rank)*(NUM_RANKS-rank) + 1;
	    moves[num_moves++].replacing = -turn;
	}
	if (file != N-1 && sq[front+1] == -turn) {
	    // may capture to right
	    moves[num_moves].from = s;
	    moves[num_moves].to = front+1;
	    moves[num_moves].value += (NUM_RANKS-rank)*(NUM_RANKS-rank) + 1;
	    moves[num_moves++].replacing = -turn;
	}
    }

    assert(num_moves <= MAX_LEGAL_MOVES);

    for (int i=0; i<num_moves-1; i++)
    	for (int j=i+1; j<num_moves; j++) {
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
	return -1;
    } else if (num_black == 0)
	return 1;

    if (turn == 1)
	base = SQ(0, NUM_RANKS-1);
    else {
	assert(turn == -1);
	base = SQ(0, 0);
    }
    for (int i=0; i<N; i++)
	if (sq[base+i] == turn)
	    return turn;
    return 0;
}

bool Pos::operator==(const Pos &a) const {
    if (turn != a.turn)
	return false;

    for (int i=0; i<NUM_ISQ; i++)
	if (sq[i] != a.sq[i])
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

Pos::Pos() {
    for (int i=0; i<NUM_ISQ; i++)
	sq[i] = 0;

    for (int i=0; i<N; i++) {
	sq[SQ(i, RANK_WHITE)] = 1;
	sq[SQ(i, RANK_BLACK)] = -1;
    }
    turn = 1;

    count_pieces();
}

pos_t Pos::pack() const {
    count_pieces();
    uint64_t base = ranks_tab.base(num_white, num_black);
    int squares[N];
    
    for (int i=0, p=0; p<num_white; i++)
	if (sq[i] == 1)
	    squares[p++] = i;
    uint64_t whites_rank = rank_combination(squares, num_white);

    for (int i=0, p=0; p<num_black; i++)
	if (sq[i] == -1)
	    squares[p++] = i;
    uint64_t blacks_rank = rank_combination(squares, num_black);

    uint64_t offset = whites_rank;
    offset = offset * binom(NUM_ISQ, num_black) + blacks_rank;
    offset = offset * 2 + (turn == -1);

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

    if (offset % 2)
	turn = -1;
    else
	turn = 1;
    offset /= 2;

    uint64_t b = binom(NUM_ISQ, num_black);
    uint64_t blacks_rank = offset%b;
    uint64_t whites_rank = offset/b;

    int squares[N];
    unrank_combination(squares, num_white, whites_rank);
    for (int i=0; i<num_white; i++) {
	assert(squares[i] >= 0);
	assert(squares[i] < NUM_ISQ);
	sq[squares[i]] = 1;
    }

    unrank_combination(squares, num_black, blacks_rank);
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

    int wc = num_white, bc = num_black;
    assert(wc == num_white);
    assert(bc == num_black);
}

ostream &Pos::print(ostream &str) const {
    char delim[N*2+2];

    for (int i=0; i<N; i++) {
	delim[i*2] = '+';
	delim[i*2+1] = '-';
    }
    delim[N*2] = '+';
    delim[N*2+1] = 0;

    for (int y=N-1; y >= 0; y--) {
	str << delim << "\n";
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
    str << delim << "\n";
    return str;
}

void Pos::clear() {
    for (int i=0; i<NUM_ISQ; i++)
	sq[i] = 0;
    num_white = num_black = 0;
}

void Pos::random_position() {
    int w=0,b=0;
    do {
	int r = rand();
	w = r % N;
	r /= N;
	b = r%N;
    } while (w == 0 && b == 0);
    random_position(w,b);
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
    int i=0;
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
    int count=0;
    int verbose=0;
    while (true) {
	Pos p;
	p.random_position();
	Pos origpos(p);
	Pos::Move moves[MAX_LEGAL_MOVES];
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

struct {
    int curr_move;
    int num_moves;
} depth_info[VERBOSE_DEPTH];

int minimax(Pos *p, int depth) {
    Pos::Move moves[MAX_LEGAL_MOVES];
    int results[MAX_LEGAL_MOVES];

    int turn = p->get_turn();
    int num_legal_moves = p->get_legal_moves(moves);

    if (depth <= VERBOSE_DEPTH)
	depth_info[depth-1].num_moves = num_legal_moves;

    //p->print(cout);

    if (num_legal_moves == 0) {
	//cout << "Game over." << endl;
	return p->winner();
    }

    for (int i=0; i<num_legal_moves; i++) {
	//cout << "Taking move " << moves[i] << endl;
	//p->check_sanity();
	if (depth <= VERBOSE_DEPTH) {
	    depth_info[depth-1].curr_move = i+1;
	    for (int j=0; j<depth; j++) {
		cout << depth_info[j].curr_move << "/"
		     << depth_info[j].num_moves << "\t";
	    }
	    for (int j=depth; j<VERBOSE_DEPTH; j++)
		cout << "\t";
	    cout << tp_table.size()/double(TP_TABLE_SIZE)*100.0 << "%" << endl;
	    // cout << "Depth " << depth << ": move "
	    // 	 << i+1 << "/" << num_legal_moves << "... " << endl;
	}
	p->do_move(moves[i]);
	pos_t packed = 0;

	bool got_result = false;

	packed = p->pack();
	if (tp_table.probe(packed, &results[i]))
	    got_result = true;

	//p->check_sanity();

	if (!got_result) {
	    results[i] = minimax(p, depth+1);
	    tp_table.add(packed, results[i]);
	}

	// if (depth <= VERBOSE_DEPTH) {
	//     cout << "Depth " << depth << ": move "
	// 	 << i+1 << "/" << num_legal_moves << " RESULT=" << results[i] << endl;
	//     size_t a = tp_table.size();
	//     cout << "Transposition table size = " << a << " ("
	// 	 << a/double(TP_TABLE_SIZE)*100.0 << "% full)" << endl;
	// }

	//cout << "Undoing move " << moves[i] << endl;
	p->undo_move(moves[i]);
	//p->check_sanity();
	if (results[i] == turn)
	    return turn; // we can force win
    }

    for (int i=0; i<num_legal_moves; i++)
	if (results[i] == 0)
	    return 0; // we can force draw

    return -turn; // we lose
}

int main() {
    //count_boards();
    //test_pack_unpack();
    //test_do_undo_move();

    //map<pos_t, int> tp_table;
    Pos p;
    int result = minimax(&p, 1);
    
    cout << "result=" << result << endl;
}
