#ifndef TranspositionTable_hpp
#define TranspositionTable_hpp

#include <cstdlib>
#include <cstdint>

// Contains everything that does not depend on capacity
class TranspositionTableBase {
protected:
    static constexpr int POS_BITS = 62;
    struct Entry { // used by many subclasses, but optional
	uint64_t pos : POS_BITS;
	unsigned result : 2; // 0 = black, 1 = draw, 2 = white, 3 = uninit
    } __attribute__ ((packed));
public:
    // returns true and assigns result if found
    virtual bool add_with_spill(uint64_t pos, int result, Entry *spilled) = 0;
    virtual bool probe(uint64_t pos, int *result) = 0; // assigns -1, 0, 1
    virtual void add(uint64_t pos, int result) = 0;
    virtual size_t size() const = 0; // estimate
    virtual size_t get_capacity() const = 0;
    virtual bool is_empty_slot(uint64_t pos) const = 0;
};

template<size_t CAPACITY>
class TranspositionTable : public TranspositionTableBase {
protected:
    size_t hash(uint64_t pos) const { return pos*21538613260663%CAPACITY; }
public:
    static constexpr size_t capacity = CAPACITY;
    size_t get_capacity() const override { return capacity; }
};

#endif
