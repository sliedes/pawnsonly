#include "binom.hpp"
#include <cstring>

uint64_t binom_tab[BINOM_MAX][BINOM_MAX];

// static inline uint64_t __attribute__ ((unused)) binom_rec(int n, int k) {
//     assert(n >= 0);
//     assert(k >= 0);
//     uint64_t v = 1;
//     for (int i=n; i >= n-k+1; i--)
// 	v *= i;
//     for (int i=2; i<=k; i++)
// 	v /= i;
//     return v;
// }

static bool is_init = false;

void init_binom() {
    if (is_init)
	return;
    is_init = true;

    memset(binom_tab, 0, sizeof(binom_tab));
    binom_tab[0][0] = 1;
    for (int n=1; n<BINOM_MAX; n++)
	for (int k=0; k<BINOM_MAX; k++) {
	    uint64_t left;
	    if (k == 0)
		left = 1;
	    else
		left = binom_tab[n-1][k-1];
	    binom_tab[n][k] = left + binom_tab[n-1][k];
	}
}

class BinomInitializer {
public:
    BinomInitializer() { init_binom(); }
} binom_initializer;
