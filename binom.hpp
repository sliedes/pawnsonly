#ifndef binom_hpp
#define binom_hpp

#include <array>
#include <cassert>
#include <cstdint>

#define BINOM_MAX 48

extern std::array<std::array<uint64_t, BINOM_MAX>, BINOM_MAX> binom_tab;

void init_binom();

static inline uint64_t binom(int n, int k) {
    assert(n >= 0);
    assert(k >= 0);
    assert(n <= BINOM_MAX);
    assert(k <= BINOM_MAX);

    if (k == 0)
	return 1;
    if (n == 0)
	return 0;

    return binom_tab[n-1][k-1];
}

// returns largest c s.t. binom(c, k) <= nn
// FIXME very slow
static inline int rev_binom_floor(uint64_t nn, int k) {
    for (int i=1;; i++)
	if (binom(i, k) > nn)
	    return i-1;
}

// len(cs) = k; cs in ascending order
static inline uint64_t rank_combination(const int *cs, int k) {
    uint64_t sum = 0;
    for (int i=0; i<k; i++)
	sum += binom(cs[i], i+1);
    return sum;
}

static inline void unrank_combination(int *cs, int k, int N) {
    for (int i=0; i<k; i++) {
	int c = rev_binom_floor(N, k-i);
	cs[i] = c;
	N -= binom(c, k-i);
    }
}

#endif
