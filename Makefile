CFLAGS=-std=gnu++11 -Wall -g -O3

OBJS=pawnsonly.o MemTranspositionTable.o binom.o

.cpp.o:
	gcc -c $< -o $@ $(CFLAGS)

pawnsonly: $(OBJS)
	g++ $^ -o $@ $(LDFLAGS)

clean:
	rm -f pawnsonly *.o

.depend: *.cpp
	gcc -std=gnu++11 -MM *.cpp >.depend

include .depend
