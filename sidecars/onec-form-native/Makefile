CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/platform_mechanism.hpp src/platform_value.hpp src/ordinary_controls.hpp

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: $(BIN)
	printf '{1,,{2,"x"}}' | $(BIN) compact | grep -q '{1,,{2,"x"}}'
	printf '{1,,{2,"x"}}' | $(BIN) listout | grep -q '{2,"x"}'
	printf '{1,,{2,"x"}}' | $(BIN) stats | grep -q '"lists":2'
	$(BIN) mechanism | grep -q '"cf_form_controls_info8":40192'
	$(BIN) mechanism | grep -q '"FormDataObject"'
	$(BIN) mechanism | grep -q '"TypeDomainPattern"'
	$(BIN) mechanism | grep -q '"core::LocalWString::serialize"'
	$(BIN) mechanism | grep -F -q '"sampleFormattedString":"{1,{1,1,{\"ru\",\"Title\"}},0}"'
	$(BIN) mechanism | grep -q '"ordinaryTransferRegistry"'
	$(BIN) mechanism | grep -q '"recordSize":40'

clean:
	rm -rf $(BUILD_DIR)
