#ifndef TranspositionTable_hpp
#define TranspositionTable_hpp

#include <cstdlib>
#include <cstdint>

class TranspositionTable {
protected:
    struct Entry { // used by many subclasses, but optional
	uint64_t pos : 62;
	unsigned result : 2; // 0 = black, 1 = draw, 2 = white, 3 = uninit
    } __attribute__ ((packed));
    const size_t tab_capacity;
    size_t hash(uint64_t pos) const { return pos%tab_capacity; }
public:
    TranspositionTable(size_t cap) : tab_capacity(cap) {}

    // returns true and assigns result if found
    virtual int probe(uint64_t pos, int *result) const = 0; // assigns -1, 0, 1
    virtual void add(uint64_t pos, int result) = 0;
    virtual size_t size() const = 0; // estimate
    size_t capacity() const { return tab_capacity; }
};


#endif
