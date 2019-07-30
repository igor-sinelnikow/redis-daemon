CC  := gcc
CXX := g++

OPT := -O3

OBJ := redisDaemon.o main.o

all: redis-daemon

redis-daemon: $(OBJ)
	$(CXX) -o $@ $(OPT) $(LDFLAGS) $^ $(shell pkg-config --libs hiredis)

main.o: main.c
	$(CC) -c $(OPT) $(CFLAGS) $<

redisDaemon.o: redisDaemon.cpp
	$(CXX) -c $(OPT) $(CXXFLAGS) $< $(shell pkg-config --cflags hiredis)

.PHONY: all clean

clean:
	rm -f $(OBJ) redis-daemon
