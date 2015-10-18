#ifndef MemTranspositionTable_hpp
#define MemTranspositionTable_hpp

#include "TranspositionTable.hpp"

#include <atomic>
#include <cassert>
#include <memory>

template<size_t CAPACITY>
class MemTranspositionTable : public TranspositionTable<CAPACITY> {
    typedef std::array<std::atomic<typename TranspositionTable<CAPACITY>::Entry>, CAPACITY> TpArray;
    std::unique_ptr<TpArray> tab;
    MemTranspositionTable(const MemTranspositionTable &);
protected:
    void write_entry(size_t n, const TranspositionTableBase::Entry &entry) override {
	(*tab)[n].store(entry, std::memory_order_relaxed);
    }
    TranspositionTableBase::Entry read_entry(size_t n) const override {
	return (*tab)[n].load(std::memory_order_relaxed);
    }
public:
    MemTranspositionTable();

    size_t size() const override; // estimate
};

template<size_t CAPACITY>
MemTranspositionTable<CAPACITY>::MemTranspositionTable() {
    tab = std::make_unique<TpArray>();
    TranspositionTableBase::Entry e;
    e.pos = 0;
    e.result = 3;
    for (size_t i=0; i<CAPACITY; i++)
	write_entry(i, e);
}

template<size_t CAPACITY>
size_t MemTranspositionTable<CAPACITY>::size() const {
    size_t count = 0;
    for (size_t i=0; i<CAPACITY/1024; i++)
	if (read_entry(i).result != 3)
	    count++;
    return count*1024;
}

#endif
