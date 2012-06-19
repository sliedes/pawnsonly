#include "FileBackedTranspositionTable.hpp"
#include <cstdio>
#include <cassert>
#include <sys/mman.h>
#include <iostream>

static inline size_t hash(uint64_t pos, size_t capacity) {
    return pos%capacity;
}

FileBackedTranspositionTable::FileBackedTranspositionTable(const char *filename) {
    fp = fopen(filename, "rb");
    assert(fp);

    fseek(fp, 0, SEEK_END);
    off_t size = ftello(fp);
    assert(size >= 0);
    assert(size >= off_t(10000*sizeof(*tab)));

    capacity_ = size/sizeof(*tab);
    tab = static_cast<TranspositionTableBase::Entry *>(mmap(NULL, capacity_*sizeof(*tab), PROT_READ|PROT_WRITE, MAP_SHARED, fileno(fp), 0));
    assert(tab);

    // Initialize
    std::cerr << "Initializing file backed tp table \""
	      << filename << "\" of " << capacity_ << " entries ("
	      << capacity_*sizeof(*tab) << " bytes)..." << std::endl;
    TranspositionTableBase::Entry e;
    e.pos = 0;
    e.result = 3;
    for (size_t i=0; i<=capacity_; i++)
	tab[i] = e;
    std::cerr << "... Done." << std::endl;
}

bool FileBackedTranspositionTable::add_with_spill(uint64_t pos, int result, Entry *spilled) {
    std::cerr << "FileBackedTranspositionTable::add_with_spill() not implemented." << std::endl;
    abort();
}

bool FileBackedTranspositionTable::probe(uint64_t pos, int *result) {
    size_t h = hash(pos, capacity_);
    if (tab[h].pos == pos) {
	*result = int(tab[h].result)-1;
	return true;
    }
    return false;
}

void FileBackedTranspositionTable::add(uint64_t pos, int result) {
    size_t h = hash(pos, capacity_);
    tab[h].pos = pos;
    tab[h].result = result-1;
}
