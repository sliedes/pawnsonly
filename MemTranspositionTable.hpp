#ifndef MemTranspositionTable_hpp
#define MemTranspositionTable_hpp

#include "TranspositionTable.hpp"
#include <vector>
#include <cassert>

template<size_t CAPACITY>
class MemTranspositionTable : public TranspositionTable<CAPACITY> {
    std::vector<typename TranspositionTable<CAPACITY>::Entry> tab;
    MemTranspositionTable(const MemTranspositionTable &);
public:
    MemTranspositionTable();

    // returns true and assigns result if found
    bool probe(uint64_t pos, int *result) override; // assigns -1, 0, 1
    void add(uint64_t pos, int result) override;
    size_t size() const override; // estimate
    bool add_with_spill(uint64_t pos, int result, TranspositionTableBase::Entry *spilled) override;
    bool is_empty_slot(uint64_t pos) const override;
};

template<size_t CAPACITY>
inline bool MemTranspositionTable<CAPACITY>::is_empty_slot(uint64_t pos) const {
    size_t h = TranspositionTable<CAPACITY>::hash(pos);
    return tab[h].result == 3;
}

template<size_t CAPACITY>
inline bool MemTranspositionTable<CAPACITY>::probe(uint64_t pos, int *result) {
    size_t h = TranspositionTable<CAPACITY>::hash(pos);
    if (tab[h].pos == pos) {
	*result = int(tab[h].result)-1;
	return 1;
    }
    return 0;
}

template<size_t CAPACITY>
inline void MemTranspositionTable<CAPACITY>::add(uint64_t pos, int result) {
    size_t h = TranspositionTable<CAPACITY>::hash(pos);
    assert(pos >> TranspositionTableBase::POS_BITS == 0);
    assert(result >= -1 && result <= 1);
    tab[h].pos = pos;
    tab[h].result = result+1;
}

template<size_t CAPACITY>
MemTranspositionTable<CAPACITY>::MemTranspositionTable() {
    TranspositionTableBase::Entry e;
    e.pos = 0;
    e.result = 3;
    tab.resize(CAPACITY, e);
    tab[0].pos = 1;
}

template<size_t CAPACITY>
size_t MemTranspositionTable<CAPACITY>::size() const {
    size_t count=0;
    for (size_t i=0; i<CAPACITY/10240; i++)
	if (tab[i].result != 3)
	    count++;
    return count*10240;
}

template<size_t CAPACITY>
bool MemTranspositionTable<CAPACITY>::add_with_spill(uint64_t pos, int result,
						     TranspositionTableBase::Entry *spilled) {
    size_t h = TranspositionTable<CAPACITY>::hash(pos);
    bool retval = false;

    assert(pos >> TranspositionTableBase::POS_BITS == 0);
    assert(result >= -1 && result <= 1);
    if (tab[h].result != 3 && tab[h].pos != pos) {
	*spilled = tab[h];
	retval = true;
    }

    tab[h].pos = pos;
    tab[h].result = result+1;
    return retval;
}


#endif
