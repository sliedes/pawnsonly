#ifndef MemTranspositionTable_hpp
#define MemTranspositionTable_hpp

#include "TranspositionTable.hpp"
#include <vector>
#include <cassert>

template<size_t CAPACITY>
class MemTranspositionTable : public TranspositionTable<CAPACITY> {
    std::vector<typename TranspositionTable<CAPACITY>::Entry> tab;
    MemTranspositionTable(const MemTranspositionTable &);
protected:
    void write_entry(size_t n, const TranspositionTableBase::Entry &entry) override { tab[n] = entry; }
    void read_entry(size_t n, TranspositionTableBase::Entry *entry) const override { *entry = tab[n]; }
public:
    MemTranspositionTable();

    size_t size() const override; // estimate
};

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
    size_t count = 0;
    for (size_t i=0; i<CAPACITY/10240; i++)
	if (tab[i].result != 3)
	    count++;
    return count*10240;
}

#endif
