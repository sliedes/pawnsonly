CFLAGS=-std=gnu++11 -Wall -g -O3

pawnsonly: pawnsonly.cpp
	g++ $< -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f pawnsonly
