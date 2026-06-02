CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/platform_mechanism.hpp src/platform_list_stream.hpp src/platform_value.hpp src/ordinary_controls.hpp src/ordinary_form_graph.hpp src/form_bin_container.hpp

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
	$(BIN) value-roundtrip | grep -F -q '"serialized":"{1,{1,2,{\"ru\",\"Title\"},{\"en\",\"Caption\"}},1}"'
	$(BIN) value-roundtrip | grep -q '"itemCount":2'
	$(BIN) controls-codec | grep -q '"controlsSize":40'
	$(BIN) controls-codec | grep -q '"positionSize":32'
	$(BIN) controls-codec | grep -q '"infoSize":16'
	$(BIN) controls-codec | grep -q '"transferSetSize":212'
	$(BIN) controls-codec | grep -q '"enumeratorHex":"030000000025000028000000010000000000000000000000000000000000000000000000000000000000000000550000200000000100000000000000000000000000000000000000000000000000000000000000009d0000100000000100000000000000000000000000000000000000000000000000000000000000"'
	$(BIN) graph-codec | grep -q '"controlCount":1'
	$(BIN) graph-codec | grep -q '"objectId":5'
	$(BIN) graph-codec | grep -F -q '"title":"{1,{1,1,{\"ru\",\"Button title\"}},0}"'
	$(BIN) graph-codec | grep -q '"transferSetSize":212'
	$(BIN) transfer-roundtrip | grep -q '"bytes":212'
	$(BIN) transfer-roundtrip | grep -q '"controls":1'
	$(BIN) transfer-roundtrip | grep -q '"controlObjectId":7'
	$(BIN) transfer-roundtrip | grep -q '"infoObjectId":7'
	$(BIN) formbin-selftest | grep -q '"byteEqual":true'
	$(BIN) formbin-selftest | grep -q '"firstName":"form"'

clean:
	rm -rf $(BUILD_DIR)
