#ifndef FileBackedTranspositionTable_hpp
#define FileBackedTranspositionTable_hpp

#include "TranspositionTable.hpp"
#include <cstdio>

class FileBackedTranspositionTable : public TranspositionTableBase {
private:
    size_t capacity_, used;
    FILE *fp;
    TranspositionTableBase::Entry *tab; // mmap()ed
public:
    FileBackedTranspositionTable(const char *filename);
    bool add_with_spill(uint64_t pos, int result, Entry *spilled) override;
    bool probe(uint64_t pos, int *result) override;
    void add(uint64_t pos, int result) override;
    size_t size() const { return 0; /* FIXME */ }
    size_t get_capacity() const { return capacity_; }
};

#endif
