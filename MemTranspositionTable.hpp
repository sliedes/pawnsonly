#ifndef MemTranspositionTable_hpp
#define MemTranspositionTable_hpp

#include "TranspositionTable.hpp"

#include <atomic>
#include <cassert>
#include <memory>

template<size_t CAPACITY>
class MemTranspositionTable : public TranspositionTable<CAPACITY> {
    typedef std::array<std::atomic<typename TranspositionTable<CAPACITY>::Entry>, CAPACITY> TpArray;
    //typedef std::array<typename TranspositionTable<CAPACITY>::Entry, CAPACITY> TpArray;
    std::unique_ptr<TpArray> tab;
    MemTranspositionTable(const MemTranspositionTable &);
protected:
    void write_entry(size_t n, const TranspositionTableBase::Entry &entry) override {
	(*tab)[n].store(entry, std::memory_order_relaxed);
	//(*tab)[n] = entry;
    }
    TranspositionTableBase::Entry read_entry(size_t n) const override {
	return (*tab)[n].load(std::memory_order_relaxed);
	//return (*tab)[n];
    }
public:
    MemTranspositionTable();

    size_t size() const override; // estimate
};

template<size_t CAPACITY>
MemTranspositionTable<CAPACITY>::MemTranspositionTable()
    : tab(std::make_unique<TpArray>())
{
    TranspositionTableBase::Entry e;
    e.pos = 0;
    e.result = static_cast<int>(TpResult::NONE);
    for (size_t i=0; i<CAPACITY; i++)
	write_entry(i, e);
}

template<size_t CAPACITY>
size_t MemTranspositionTable<CAPACITY>::size() const {
    size_t count = 0;
    for (size_t i=0; i<CAPACITY/10240; i++)
	if (TpResult(read_entry(i).result) != TpResult::NONE)
	    count++;
    return count*10240;
}

#endif
