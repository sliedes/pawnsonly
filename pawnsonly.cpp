#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <algorithm>

#define DEBUG 1

using std::ostream;
using std::cout;
using std::endl;
using std::cerr;

// board size (number of pawns per side). Must be >= 4.
static const int N = 8;

// number of internal ranks (i.e. those on which pawns can be
// without the game being over)
static const int NUM_RANKS = N-2;

// number of internal squares 
static const int NUM_ISQ = N*NUM_RANKS;

// starting ranks
static const int RANK_WHITE = 0;
static const int RANK_BLACK = NUM_RANKS-1;

static const int MAX_LEGAL_MOVES = N*2;

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

static const int SQ(int x, int y) {
    assert(x >= 0);
    assert(x < N);
    assert(y >= 0);
    assert(y < N-2);

    return y*N+x;
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
    Pos(); // initial position
    Pos(pos_t);
    pos_t pack() const;
    void check_sanity();
    ostream &print(ostream &str) const;
    void random_position();
    void random_position(int nwhites, int nblacks);

    bool operator==(const Pos &) const;
};

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
    int wc = num_white, bc = num_black;
    force_count_pieces();
    assert(wc == -1 || wc == num_white);
    assert(bc == -1 || bc == num_black);
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
    int r = rand();
    int w = r % N;
    r /= N;
    int b = r%N;
    random_position(w,b);
}

void Pos::random_position(int nw, int nb) {
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

int main() {
    //count_boards();
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
