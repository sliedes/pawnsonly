#ifndef MemTranspositionTable_hpp
#define MemTranspositionTable_hpp

#include "TranspositionTable.hpp"
#include <vector>
#include <cassert>

class MemTranspositionTable : public TranspositionTable {
    std::vector<Entry> tab;
    static MemTranspositionTable *instance;
    MemTranspositionTable(const MemTranspositionTable &);
public:
    MemTranspositionTable(size_t cap);

    // returns true and assigns result if found
    int probe(uint64_t pos, int *result) const; // assigns -1, 0, 1
    void add(uint64_t pos, int result);
    size_t size() const; // estimate
};

inline int MemTranspositionTable::probe(uint64_t pos, int *result) const {
    size_t h = hash(pos);
    if (tab[h].pos == pos) {
	*result = int(tab[h].result)-1;
	return 1;
    }
    return 0;
}

inline void MemTranspositionTable::add(uint64_t pos, int result) {
    size_t h = hash(pos);
    assert(pos >> 62 == 0);
    assert(result >= -1 && result <= 1);
    tab[h].pos = pos;
    tab[h].result = result+1;
}

#endif
