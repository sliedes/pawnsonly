#ifndef TranspositionTable_hpp
#define TranspositionTable_hpp

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Contains everything that does not depend on capacity
class TranspositionTableBase {
protected:
    static constexpr int POS_BITS = 30;
    typedef uint32_t saved_pos_t;
    struct Entry { // used by many subclasses, but optional
	saved_pos_t pos : POS_BITS;
	unsigned result : 2; // 0 = black, 1 = draw, 2 = white, 3 = uninit
    } __attribute__ ((packed));

    virtual saved_pos_t pos_to_saved(uint64_t pos) const = 0;
    virtual uint64_t saved_to_pos(saved_pos_t saved, size_t hash_slot) const = 0;
public:
    // returns true and assigns result if found
    virtual bool add_with_spill(uint64_t pos, int result, Entry *spilled) = 0;
    virtual bool probe(uint64_t pos, int *result) = 0; // assigns -1, 0, 1
    virtual void add(uint64_t pos, int result) = 0;
    virtual size_t size() const = 0; // estimate
    virtual size_t get_capacity() const = 0;
    virtual bool is_empty_slot(uint64_t pos) const = 0;
    virtual void save(const char *fname) const = 0;
    virtual void load(const char *fname) = 0;
};

template<size_t CAPACITY>
class TranspositionTable : public TranspositionTableBase {
protected:
    //size_t hash(uint64_t pos) const { return pos*21538613260663%CAPACITY; }
    size_t hash(uint64_t pos) const { return pos%CAPACITY; }
    virtual void write_entry(size_t n, const TranspositionTableBase::Entry &entry) = 0;
    virtual TranspositionTableBase::Entry read_entry(size_t n) const = 0;
    saved_pos_t pos_to_saved(uint64_t pos) const override;
    uint64_t saved_to_pos(saved_pos_t saved, size_t hash_slot) const override;
public:
    static constexpr size_t capacity = CAPACITY;
    size_t get_capacity() const override { return capacity; }
    bool is_empty_slot(uint64_t pos) const override;
    // returns true and assigns result if found
    bool probe(uint64_t pos, int *result) override; // assigns -1, 0, 1
    void add(uint64_t pos, int result) override;
    bool add_with_spill(uint64_t pos, int result, TranspositionTableBase::Entry *spilled) override;
    void save(const char *fname) const override;
    void load(const char *fname) override;
};

template<size_t CAPACITY>
TranspositionTableBase::saved_pos_t TranspositionTable<CAPACITY>::pos_to_saved(uint64_t pos) const {
    uint64_t a = pos/CAPACITY;
    assert(a < (1 << POS_BITS));
    return saved_pos_t(a);
}

template<size_t CAPACITY>
uint64_t TranspositionTable<CAPACITY>::saved_to_pos(saved_pos_t a, size_t hash_slot) const {
    return a*CAPACITY + hash_slot;
}

template<size_t CAPACITY>
bool TranspositionTable<CAPACITY>::is_empty_slot(uint64_t pos) const {
    Entry e = read_entry(hash(pos));
    return e.result == 3;
}

template<size_t CAPACITY>
inline bool TranspositionTable<CAPACITY>::probe(uint64_t pos, int *result) {
    Entry e = read_entry(hash(pos));
    if (e.result == 3)
	return 0;
    saved_pos_t saved_pos = pos_to_saved(pos);
    if (e.pos == saved_pos) {
	*result = int(e.result)-1;
	return 1;
    }
    return 0;
}

template<size_t CAPACITY>
inline void TranspositionTable<CAPACITY>::add(uint64_t pos, int result) {
    assert(result >= -1 && result <= 1);
    Entry e;
    uint64_t saved_pos = pos_to_saved(pos);
    assert(saved_pos >> TranspositionTableBase::POS_BITS == 0);
    e.pos = saved_pos_t(saved_pos);
    e.result = result+1;
    write_entry(hash(pos), e);
}

template<size_t CAPACITY>
bool TranspositionTable<CAPACITY>::add_with_spill(uint64_t pos, int result,
						  TranspositionTableBase::Entry *spilled) {
    assert(result >= -1 && result <= 1);
    uint64_t saved_pos = pos_to_saved(pos);
    assert(saved_pos >> TranspositionTableBase::POS_BITS == 0);

    size_t h = hash(pos);
    bool retval = false;

    Entry e = read_entry(h);

    if (e.result != 3 && e.pos != saved_pos) {
	*spilled = e;
	retval = true;
    }

    e.pos = saved_pos;
    e.result = result+1;
    write_entry(h, e);
    return retval;
}

template<size_t CAPACITY>
void TranspositionTable<CAPACITY>::save(const char *fname) const {
    FILE *fp = fopen(fname, "wb");
    assert(fp && "Could not open save file for write");

    // write capacity
    {
	const size_t cap = CAPACITY;
	size_t res = fwrite(&cap, sizeof(cap), 1, fp);
	if (res != 1) {
	    std::cerr << "Short write" << std::endl;
	    abort();
	}
    }

    // FIXME slow
    for (size_t i = 0; i < CAPACITY; i++) {
	Entry e = read_entry(i);
	size_t res = fwrite(&e, sizeof(e), 1, fp);
	if (res != 1) {
	    std::cerr << "Short write" << std::endl;
	    abort();
	}
    }

    size_t res = fclose(fp);
    if (res != 0) {
	std::cerr << "Failed to close save file" << std::endl;
	abort();
    }
}

template<size_t CAPACITY>
void TranspositionTable<CAPACITY>::load(const char *fname) {
    FILE *fp = fopen(fname, "rb");
    assert(fp && "Could not open save file for read.");

    // read capacity
    {
	size_t cap;
	size_t res = fread(&cap, sizeof(cap), 1, fp);
	if (res != 1) {
	    std::cerr << "Short read" << std::endl;
	    abort();
	}
	if (cap != CAPACITY) {
	    std::cerr << "Wrong capacity in save file" << std::endl;
	    abort();
	}
    }

    // FIXME slow
    for (size_t i = 0; i < CAPACITY; i++) {
	Entry e;
	size_t res = fread(&e, sizeof(e), 1, fp);
	if (res != 1) {
	    std::cerr << "Short read" << std::endl;
	    abort();
	}
	write_entry(i, e);
    }

    size_t res = fclose(fp);
    if (res != 0) {
	std::cerr << "Failed to close save file" << std::endl;
	abort();
    }
}

#endif
