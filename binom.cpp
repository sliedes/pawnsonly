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

#include "binom.hpp"
#include <array>
#include <cstring>

std::array<std::array<uint64_t, BINOM_MAX>, BINOM_MAX> binom_tab;

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

    // std::array is guaranteed to be layout compatible with C array
    memset(binom_tab.data(), 0, sizeof(binom_tab));

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
