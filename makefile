CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
CPPFLAGS += -Iinclude -Ithird_party
SFML_LIBS = -lsfml-graphics -lsfml-window -lsfml-system
HEADERS = $(wildcard include/pwg/*.hpp) third_party/FastNoise.h

all: ProcWorld

FastNoise.o: third_party/FastNoise.cpp third_party/FastNoise.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -w -c -o $@ $<

ProcWorld: main.cpp FastNoise.o $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ main.cpp FastNoise.o $(SFML_LIBS) -lz

# no SFML needed: --export / --stats only
headless: main.cpp FastNoise.o $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DPWG_NO_SFML -o ProcWorld-headless main.cpp FastNoise.o -lz

pwg_tests: tests/test_world.cpp FastNoise.o $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ tests/test_world.cpp FastNoise.o -lz

test: pwg_tests
	./pwg_tests

clean:
	rm -f ProcWorld ProcWorld-headless pwg_tests *.o

.PHONY: all headless test clean
