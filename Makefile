CXX = /c/msys64/ucrt64/bin/g++
CXXFLAGS = -std=c++17 -O3 -march=native -flto -DNDEBUG
LDFLAGS = -lpthread

SRC = main.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp search.cpp uci.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = chesscpp.exe

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug: CXXFLAGS = -std=c++17 -g -O0 -Wall -Wextra
debug: clean all

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean debug
