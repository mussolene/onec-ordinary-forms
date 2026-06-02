CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/platform_mechanism.hpp src/platform_list_stream.hpp src/platform_value.hpp src/ordinary_controls.hpp src/ordinary_form_graph.hpp src/form_bin_container.hpp src/platform_guid_registry.hpp

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
	$(BIN) mechanism | grep -q '"formatEntry":40'
	$(BIN) mechanism | grep -F -q '"countSemantics":"payload byte size"'
	$(BIN) mechanism | grep -F -q '"boundary":"existing IFile/HGLOBAL payload returned by FUN_00270da0"'
	$(BIN) value-roundtrip | grep -F -q '"serialized":"{1,{1,2,{\"ru\",\"Title\"},{\"en\",\"Caption\"}},1}"'
	$(BIN) value-roundtrip | grep -q '"itemCount":2'
	$(BIN) controls-codec | grep -q '"diagnosticControlsFixtureSize":40'
	$(BIN) controls-codec | grep -q '"positionSize":32'
	$(BIN) controls-codec | grep -q '"infoSize":16'
	$(BIN) controls-codec | grep -q '"transferSetSize":212'
	$(BIN) controls-codec | grep -q '"enumeratorHex":"030000000055000020000000010000000000000000000000000000000000000000000000000000000000000000250000000000002800000000000000000000000000000000000000000000000000000000000000009d0000100000000100000000000000000000000000000000000000000000000000000000000000"'
	$(BIN) graph-codec | grep -q '"controlCount":1'
	$(BIN) graph-codec | grep -q '"objectId":5'
	$(BIN) graph-codec | grep -F -q '"title":"{1,{1,1,{\"ru\",\"Button title\"}},0}"'
	$(BIN) graph-codec | grep -q '"transferSetSize":212'
	$(BIN) transfer-roundtrip | grep -q '"bytes":212'
	$(BIN) transfer-roundtrip | grep -q '"controlsBytes":40'
	$(BIN) transfer-roundtrip | grep -q '"diagnosticControlObjectId":7'
	$(BIN) transfer-roundtrip | grep -q '"infoObjectId":7'
	$(BIN) transfer-sections | grep -F -q '"facet":"controls"'
	$(BIN) transfer-sections | grep -F -q '"recordSize":0'
	$(BIN) transfer-sections | grep -F -q '"countOrBytes":40'
	$(BIN) formbin-selftest | grep -q '"byteEqual":true'
	$(BIN) formbin-selftest | grep -q '"firstName":"form"'
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -q '"09ccdc77-ea1a-4a6d-ab1c-3435eada2433"'; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -F -q '"repeatedGuidBlocks":["0x42370"'; fi

clean:
	rm -rf $(BUILD_DIR)
