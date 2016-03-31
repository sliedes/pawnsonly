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

#ifndef TranspositionTable_hpp
#define TranspositionTable_hpp

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#define DEBUG_POSITION 0

static constexpr bool DEBUG_TP = false;

enum class TpResult {
    NONE = 0,
    CURRENT_LOSS = 1,
    DRAW = 2,
    CURRENT_WIN = 3,
    LOWER_BOUND_0 = 4,
    UPPER_BOUND_0 = 5
};

static inline TpResult flip_result(TpResult a) {
    static const TpResult flipped[] = {
	TpResult::NONE, TpResult::CURRENT_WIN, TpResult::DRAW,
	TpResult::CURRENT_LOSS, TpResult::UPPER_BOUND_0,
	TpResult::LOWER_BOUND_0};
    return flipped[static_cast<int>(a)];
}

static inline TpResult merge_results(TpResult a, TpResult b) {
    if (a == b)
	return a;
    if (b == TpResult::NONE)
	return a;

    switch(a) {
    case TpResult::NONE:
	return b;
    case TpResult::CURRENT_LOSS:
	if (b == TpResult::UPPER_BOUND_0)
	    return TpResult::CURRENT_LOSS;
	break;
    case TpResult::DRAW:
	if (b == TpResult::LOWER_BOUND_0 || b == TpResult::UPPER_BOUND_0)
	    return TpResult::DRAW;
    case TpResult::CURRENT_WIN:
	if (b == TpResult::LOWER_BOUND_0)
	    return TpResult::CURRENT_WIN;
    case TpResult::LOWER_BOUND_0:
	if (b == TpResult::UPPER_BOUND_0)
	    return TpResult::DRAW;
	else if (b == TpResult::DRAW || b == TpResult::CURRENT_WIN)
		return b;
	break;
    case TpResult::UPPER_BOUND_0:
	if (b == TpResult::LOWER_BOUND_0)
	    return TpResult::DRAW;
	else if (b == TpResult::DRAW || b == TpResult::CURRENT_LOSS)
	    return b;
	break;
    }

    std::cout << "merge_result: conflicting results "
	      << static_cast<int>(a) << ", " << static_cast<int>(b)
	      << std::endl;
    assert(false);
    abort();
}


// Contains everything that does not depend on capacity
class TranspositionTableBase {
protected:
    static constexpr int POS_BITS = 29;
    typedef uint32_t saved_pos_t;
    struct Entry { // used by many subclasses, but optional
	saved_pos_t pos : POS_BITS;
	unsigned result : 3; // actually a TpResult
    } __attribute__ ((packed));

    virtual saved_pos_t pos_to_saved(uint64_t pos) const = 0;
    virtual uint64_t saved_to_pos(saved_pos_t saved, size_t hash_slot) const = 0;
public:
    virtual void add(uint64_t pos, TpResult result) = 0;
    virtual TpResult probe(uint64_t pos) = 0;
    virtual size_t size() const = 0; // estimate
    virtual size_t get_capacity() const = 0;
    virtual bool is_empty_slot(uint64_t pos) const = 0;
    virtual void save(const char *fname) const = 0;
    virtual void load(const char *fname) = 0;
};

template<size_t CAPACITY>
class TranspositionTable : public TranspositionTableBase {
protected:
    //size_t hash(uint64_t pos) const { return pos*21538613260663%CAPACITY; }
    size_t hash(uint64_t pos) const { return pos%CAPACITY; }
    virtual void write_entry(size_t n, const TranspositionTableBase::Entry &entry) = 0;
    virtual TranspositionTableBase::Entry read_entry(size_t n) const = 0;
    saved_pos_t pos_to_saved(uint64_t pos) const override;
    uint64_t saved_to_pos(saved_pos_t saved, size_t hash_slot) const override;
public:
    static constexpr size_t capacity = CAPACITY;
    size_t get_capacity() const override { return capacity; }
    bool is_empty_slot(uint64_t pos) const override;
    void add(uint64_t pos, TpResult result) override;
    TpResult probe(uint64_t pos) override;
    void save(const char *fname) const override;
    void load(const char *fname) override;
};

template<size_t CAPACITY>
TranspositionTableBase::saved_pos_t TranspositionTable<CAPACITY>::pos_to_saved(uint64_t pos) const {
    uint64_t a = pos/CAPACITY;
    assert(a < (1 << POS_BITS));
    return saved_pos_t(a);
}

template<size_t CAPACITY>
uint64_t TranspositionTable<CAPACITY>::saved_to_pos(saved_pos_t a, size_t hash_slot) const {
    return a*CAPACITY + hash_slot;
}

template<size_t CAPACITY>
bool TranspositionTable<CAPACITY>::is_empty_slot(uint64_t pos) const {
    Entry e = read_entry(hash(pos));
    return TpResult(e.result) == TpResult::NONE;
}

template<size_t CAPACITY>
inline TpResult TranspositionTable<CAPACITY>::probe(uint64_t pos) {
    Entry e = read_entry(hash(pos));
    TpResult res = TpResult(e.result);
    saved_pos_t saved_pos = pos_to_saved(pos);
    if (e.pos != saved_pos)
	return TpResult::NONE;
    return res;
}

template<size_t CAPACITY>
inline void TranspositionTable<CAPACITY>::add(uint64_t pos, TpResult result) {
    if (DEBUG_POSITION != 0 && pos == DEBUG_POSITION) {
	std::cout << "Add position " << pos << " with result " << static_cast<int>(result)
		  << std::endl;
    }

    assert(result != TpResult::NONE);
    Entry e;
    uint64_t saved_pos = pos_to_saved(pos);
    assert(saved_pos >> TranspositionTableBase::POS_BITS == 0);
    e.pos = saved_pos_t(saved_pos);
    e.result = static_cast<int>(result);

    const size_t ha = hash(pos);

    if (result == TpResult::LOWER_BOUND_0 || result == TpResult::UPPER_BOUND_0 || DEBUG_TP) {
	Entry old = read_entry(ha);
	if (old.pos == e.pos)
	    e.result = static_cast<int>(merge_results(result, TpResult(old.result)));
    }

    write_entry(ha, e);
}

template<size_t CAPACITY>
void TranspositionTable<CAPACITY>::save(const char *fname) const {
    FILE *fp = fopen(fname, "wb");
    assert(fp && "Could not open save file for write");

    // write capacity
    {
	const size_t cap = CAPACITY;
	size_t res = fwrite(&cap, sizeof(cap), 1, fp);
	if (res != 1) {
	    std::cerr << "Short write" << std::endl;
	    abort();
	}
    }

    // FIXME slow
    for (size_t i = 0; i < CAPACITY; i++) {
	Entry e = read_entry(i);
	size_t res = fwrite(&e, sizeof(e), 1, fp);
	if (res != 1) {
	    std::cerr << "Short write" << std::endl;
	    abort();
	}
    }

    size_t res = fclose(fp);
    if (res != 0) {
	std::cerr << "Failed to close save file" << std::endl;
	abort();
    }
}

template<size_t CAPACITY>
void TranspositionTable<CAPACITY>::load(const char *fname) {
    FILE *fp = fopen(fname, "rb");
    assert(fp && "Could not open save file for read.");

    // read capacity
    {
	size_t cap;
	size_t res = fread(&cap, sizeof(cap), 1, fp);
	if (res != 1) {
	    std::cerr << "Short read" << std::endl;
	    abort();
	}
	if (cap != CAPACITY) {
	    std::cerr << "Wrong capacity in save file" << std::endl;
	    abort();
	}
    }

    // FIXME slow
    for (size_t i = 0; i < CAPACITY; i++) {
	Entry e;
	size_t res = fread(&e, sizeof(e), 1, fp);
	if (res != 1) {
	    std::cerr << "Short read" << std::endl;
	    abort();
	}
	write_entry(i, e);
    }

    size_t res = fclose(fp);
    if (res != 0) {
	std::cerr << "Failed to close save file" << std::endl;
	abort();
    }
}

#endif
