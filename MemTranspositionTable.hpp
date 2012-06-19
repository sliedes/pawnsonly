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
    int probe(uint64_t pos, int *result) const; // assigns -1, 0, 1
    void add(uint64_t pos, int result);
    size_t size() const; // estimate
};

template<size_t CAPACITY>
inline int MemTranspositionTable<CAPACITY>::probe(uint64_t pos, int *result) const {
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
    assert(pos >> 62 == 0);
    assert(result >= -1 && result <= 1);
    tab[h].pos = pos;
    tab[h].result = result+1;
}

template<size_t CAPACITY>
MemTranspositionTable<CAPACITY>::MemTranspositionTable() {
    tab.resize(CAPACITY);
    for (size_t i=0; i<CAPACITY; i++)
	tab[i].result = 3;
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


#endif
