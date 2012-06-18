#include "MemTranspositionTable.hpp"

MemTranspositionTable *MemTranspositionTable::instance = nullptr;

MemTranspositionTable::MemTranspositionTable(size_t cap) : TranspositionTable(cap) {
    assert(!instance);
    instance = this;
    tab.resize(tab_capacity);
    for (size_t i=0; i<tab_capacity; i++)
	tab[i].result = 3;
    tab[0].pos = 1;
}

size_t MemTranspositionTable::size() const {
    size_t count=0;
    for (size_t i=0; i<tab_capacity/10240; i++)
	if (tab[i].result != 3)
	    count++;
    return count*10240;
}

