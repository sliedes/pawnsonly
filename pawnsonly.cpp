#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <sstream>

#define DEBUG 1

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

static uint64_t binom(int n, int k) {
    assert(n >= 0);
    assert(k >= 0);
    uint64_t v = 1;
    for (int i=n; i >= n-k+1; i--)
	v *= i;
    for (int i=2; i<=k; i++)
	v /= i;
    return v;
}


// returns largest c s.t. binom(c, k) <= N
static int rev_binom_floor(uint64_t N, int k) {
    for (int i=1;; i++)
	if (binom(i, k) > N)
	    return i-1;
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
    uint64_t base(int nwhite, int nblack) {
	assert(nwhite >= 0 && nwhite <= N);
	assert(nblack >= 0 && nblack <= N);
	return tab[nwhite*(N+1)+nblack];
    }
    // returns the index of the first element >= n
    int find(uint64_t n) const {
	return std::upper_bound(tab, tab+SIZE, n) - tab - 1;
    }
} ranks_tab;

Compact_tab *Compact_tab::instance = nullptr;

Compact_tab::Compact_tab() {
    instance = this;
    int p=0;
    tab[p++] = 0;
    for (int white=0; white<=N; white++)
	for (int black=0; black<=N; black++) {
	    if (white == 0 && black == 0)
		continue;
	    tab[p] = tab[p-1] + binom(NUM_ISQ, white) * binom(NUM_ISQ, black);
	    p++;
	}
    assert(p == SIZE);
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
	int from, to, replacing;
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

    int home_start, home_end;
    if (turn == 1)
	home_start = 0;
    else {
	assert(turn == -1);
	home_start = SQ(0, NUM_RANKS-1);
    }
    home_end = home_start + N-1;

    for (int i=0; i<num_pawns; i++) {
	int s = positions[i], file = s%N;
	int front = s + turn*N; // sq in front of current
	if (sq[front] == 0) {
	    // front square empty, add it
	    moves[num_moves].from = s;
	    moves[num_moves].to = front;
	    moves[num_moves++].replacing = 0;
	    if (N >= 5 && s >= home_start && s <= home_end) {
		// move ahead 2 squares?
		int front2 = front + turn*N;
		assert(front2 >= 0);
		assert(front2 < NUM_ISQ);
		if (sq[front2] == 0) {
		    moves[num_moves].from = s;
		    moves[num_moves].to = front2;
		    moves[num_moves++].replacing = 0;
		}
	    }
	}
	if (file != 0 && sq[front-1] == -turn) {
	    // may capture to left
	    moves[num_moves].from = s;
	    moves[num_moves].to = front-1;
	    moves[num_moves++].replacing = -turn;
	}
	if (file != N-1 && sq[front+1] == -turn) {
	    // may capture to right
	    moves[num_moves].from = s;
	    moves[num_moves].to = front+1;
	    moves[num_moves++].replacing = -turn;
	}
    }

    assert(num_moves <= MAX_LEGAL_MOVES);
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

// len(cs) = k; cs in ascending order
static uint64_t rank_combination(const int *cs, int k) {
    uint64_t sum = 0;
    for (int i=0; i<k; i++)
	sum += binom(cs[i], i+1);
    return sum;
}

static void unrank_combination(int *cs, int k, int N) {
    for (int i=0; i<k; i++) {
	int c = rev_binom_floor(N, k-i);
	cs[i] = c;
	N -= binom(c,k-i);
    }
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

    if (DEBUG) {
	if (num_white != 8 || num_black != 8) {
	    uint64_t base_range = ranks_tab[ranks_tab.find(base)+1]-ranks_tab[ranks_tab.find(base)];
	    assert(offset < base_range);
	}
    }

    if (ranks_tab.find(base + offset) != num_white*(N+1)+num_black) {
	print(cerr);
	cerr << "base = " << base << endl;
	cerr << "offset = " << offset << endl;
	cerr << "whites_rank = " << whites_rank << endl;
	cerr << "blacks_rank = " << blacks_rank << endl;
	cerr << "num_white*(N+1)+num_black = " << num_white*(N+1)+num_black << endl;
	cerr << "ranks_tab.find(base) = " << ranks_tab.find(base) << endl;
	cerr << "ranks_tab@base = " << ranks_tab[ranks_tab.find(base)] << endl;
	cerr << "base_range = " << ranks_tab[ranks_tab.find(base)+1]-ranks_tab[ranks_tab.find(base)] << endl;
	cerr << "ranks_tab.find(base + offset) = " << ranks_tab.find(base+offset) << endl;
	cerr << "ranks_tab.find(base + offset) = " << ranks_tab[ranks_tab.find(base+offset)] << endl;
	abort();
    }
    return base + offset;
}

Pos::Pos(pos_t compact) {
    clear();

    uint64_t idx = ranks_tab.find(compact);
    uint64_t base = ranks_tab[idx];
    uint64_t offset = compact-base;

    num_black = idx%(N+1);
    num_white = idx/(N+1);

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
    for (int i=0; i<num_white; i++)
	sq[squares[i]] = 1;

    unrank_combination(squares, num_black, blacks_rank);
    for (int i=0; i<num_black; i++)
	sq[squares[i]] = -1;
    
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

void pack_unpack_random_positions() {
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

int minimax(Pos *p) {
    Pos::Move moves[MAX_LEGAL_MOVES];
    int results[MAX_LEGAL_MOVES];

    int turn = p->get_turn();
    int num_legal_moves = p->get_legal_moves(moves);

    //p->print(cout);

    if (num_legal_moves == 0) {
	//cout << "Game over." << endl;
	return p->winner();
    }

    for (int i=0; i<num_legal_moves; i++) {
	//cout << "Taking move " << moves[i] << endl;
	p->check_sanity();
	p->do_move(moves[i]);
	p->check_sanity();
	results[i] = minimax(p);
	//cout << "Undoing move " << moves[i] << endl;
	p->undo_move(moves[i]);
	p->check_sanity();
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

    //test_do_undo_move();

    Pos p;
    int result = minimax(&p);
    
    cout << "result=" << result << endl;
}
