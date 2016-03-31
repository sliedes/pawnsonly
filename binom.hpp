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
