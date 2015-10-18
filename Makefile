CXXFLAGS=-std=c++14 -Wall -g -O3
LDFLAGS=-latomic -lpthread
CXX=g++

OBJS=pawnsonly.o binom.o FileBackedTranspositionTable.o

.cpp.o:
	$(CXX) -c $< -o $@ $(CXXFLAGS)

pawnsonly: $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	rm -f pawnsonly *.o

.depend: *.cpp
	$(CXX) -std=gnu++11 -MM *.cpp >.depend

include .depend
