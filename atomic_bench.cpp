#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>

struct Entry {
    uint32_t pos;
} __attribute__ ((packed));

constexpr size_t N = 1000000000;

std::array<std::atomic<Entry>, N> *arr;

int main() {
    arr = new std::array<std::atomic<Entry>, N>();

    std::cout << "lock_free(): " << (*arr)[100].is_lock_free() << std::endl;

    for (size_t i=0; i<N; i++) {
	Entry e;
	e.pos = 1;
	(*arr)[i].store(e, std::memory_order_relaxed);
    }
}
