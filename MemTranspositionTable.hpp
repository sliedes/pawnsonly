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
