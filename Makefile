CXXFLAGS=-std=gnu++11 -Wall -g -O3

OBJS=pawnsonly.o binom.o FileBackedTranspositionTable.o

.cpp.o:
	gcc -c $< -o $@ $(CXXFLAGS)

pawnsonly: $(OBJS)
	g++ $^ -o $@ $(LDFLAGS)

clean:
	rm -f pawnsonly *.o

.depend: *.cpp
	gcc -std=gnu++11 -MM *.cpp >.depend

include .depend
