CXXFLAGS=-std=c++14 -Wall -g -O3
LDFLAGS=-latomic -lpthread
CXX=g++

OBJS=pawnsonly.o binom.o

all: pawnsonly atomic_bench.clang atomic_bench.gcc

.cpp.o:
	$(CXX) -c $< -o $@ $(CXXFLAGS)

pawnsonly: $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

atomic_bench.clang: atomic_bench.cpp
	clang++ $< -o $@ $(CXXFLAGS) $(LDFLAGS)

atomic_bench.gcc: atomic_bench.cpp
	g++ $< -o $@ $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f pawnsonly atomic_bench.clang atomic_bench.gcc *.o

.depend: *.cpp
	$(CXX) -std=gnu++11 -MM *.cpp >.depend

include .depend
