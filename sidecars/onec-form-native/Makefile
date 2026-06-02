CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: $(BIN)
	printf '{1,,{2,"x"}}' | $(BIN) compact | grep -q '{1,,{2,"x"}}'
	printf '{1,,{2,"x"}}' | $(BIN) listout | grep -q '{2,"x"}'
	printf '{1,,{2,"x"}}' | $(BIN) stats | grep -q '"lists":2'

clean:
	rm -rf $(BUILD_DIR)
