#ifndef CachedTranspositionTable_hpp
#define CachedTranspositionTable_hpp

#include "TranspositionTable.hpp"

// "Cached" is a misnomer in a sense; the entries in the cache are
// only spilled to the backing store when overwritten.
template<typename CacheT, typename BackingT>
class CachedTranspositionTable : public TranspositionTableBase {
    CacheT cache;
    BackingT backing;
    bool add_with_spill(uint64_t pos, int result, TranspositionTableBase::Entry *spilled) override;
public:
    bool probe(uint64_t pos, int *result) override;
    void add(uint64_t pos, int result) override;
    size_t size() const override;
};

template<typename CacheT, typename BackingT>
bool CachedTranspositionTable<CacheT, BackingT>::probe(uint64_t pos, int *result) {
    if (cache.probe(pos, result))
	return true;

    if (backing.probe(pos, result)) {
	add(pos, *result);
	return true;
    }

    return false;
}

template<typename CacheT, typename BackingT>
void CachedTranspositionTable<CacheT, BackingT>::add(uint64_t pos, int result) {
    TranspositionTableBase::Entry spilled;
    if (cache.add_with_spill(pos, result, &spilled))
	backing.add(spilled.pos, int(spilled.result)-1);
}


template<typename CacheT, typename BackingT>
bool CachedTranspositionTable<CacheT, BackingT>
::add_with_spill(uint64_t pos, int result, TranspositionTableBase::Entry *spilled) {
    if (cache.add_with_spill(pos, result, spilled))
	return backing.add_with_spill(spilled->pos, int(spilled->result)-1, spilled);
    else
	return false;
}


template<typename CacheT, typename BackingT>
size_t CachedTranspositionTable<CacheT, BackingT>::size() const {
    return cache.size() + backing.size();
}


#endif
