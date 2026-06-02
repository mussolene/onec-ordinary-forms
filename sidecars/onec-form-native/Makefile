CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lz

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/platform_mechanism.hpp src/platform_list_stream.hpp src/platform_value.hpp src/platform_value_metadata.hpp src/platform_descriptor_registry.hpp src/platform_form_schema.hpp src/ordinary_controls.hpp src/ordinary_form_graph.hpp src/form_bin_container.hpp src/platform_guid_registry.hpp

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

test: $(BIN)
	printf '{1,,{2,"x"}}' | $(BIN) compact | grep -q '{1,,{2,"x"}}'
	printf '{1,,{2,"x"}}' | $(BIN) listout | grep -q '{2,"x"}'
	printf '{1,,{2,"x"}}' | $(BIN) stats | grep -q '"lists":2'
	$(BIN) mechanism | grep -q '"cf_form_controls_info8":40192'
	$(BIN) mechanism | grep -q '"FormDataObject"'
	$(BIN) mechanism | grep -q '"TypeDomainPattern"'
	$(BIN) mechanism | grep -q '"core::LocalWString::serialize"'
	$(BIN) mechanism | grep -F -q '"sampleFormattedString":"{1,{1,1,{\"ru\",\"Title\"}},0}"'
	$(BIN) mechanism | grep -q '"schemaValueSurface"'
	$(BIN) mechanism | grep -F -q '"schemaSource":"xdto_root.res:data_ui.xsd Color"'
	$(BIN) mechanism | grep -F -q '"platformEvidence":"core82 Font::serialize/deserialize list arity 6"'
	$(BIN) mechanism | grep -F -q '"layoutStatus":"trace-backed list skeleton; real payload correlation pending"'
	$(BIN) mechanism | grep -q '"ordinaryTransferRegistry"'
	$(BIN) mechanism | grep -q '"formatEntry":40'
	$(BIN) mechanism | grep -F -q '"countSemantics":"payload byte size"'
	$(BIN) mechanism | grep -F -q '"boundary":"existing IFile/HGLOBAL payload returned by FUN_00270da0"'
	$(BIN) platform-form-schema | grep -F -q '"source":"mngcore_root.res:http://v8.1c.ru/8.2/managed-application/logform/layouter"'
	$(BIN) platform-form-schema | grep -F -q '"controlCount":24'
	$(BIN) platform-form-schema | grep -F -q '"directGuidBindings":0'
	$(BIN) platform-form-schema | grep -F -q '"guidBindingStatus":"not-present-in-xsd; use resource/binary evidence"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"TextBox"'
	$(BIN) platform-form-schema | grep -F -q '"streamElement":"txt"'
	$(BIN) platform-form-schema | grep -F -q '"valueTypes":"Value,ui:Picture,v81:UUID,ui:Color,ui:Font,ui:ShortCutType,ContextMenu"'
	$(BIN) value-roundtrip | grep -F -q '"serialized":"{1,{1,2,{\"ru\",\"Title\"},{\"en\",\"Caption\"}},1}"'
	$(BIN) value-roundtrip | grep -q '"itemCount":2'
	$(BIN) value-roundtrip | grep -F -q '"compositeID":"{42,09ccdc77-ea1a-4a6d-ab1c-3435eada2433}"'
	$(BIN) value-roundtrip | grep -F -q '"typeDomainPattern":"{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}"'
	$(BIN) value-roundtrip | grep -F -q '"genericValue":"{\"String\",\"Value\"}"'
	$(BIN) value-roundtrip | grep -F -q '"colorSchema":"#123456"'
	$(BIN) value-roundtrip | grep -F -q '"fontFace":"Arial"'
	$(BIN) value-roundtrip | grep -F -q '"borderStyle":"Single"'
	$(BIN) value-roundtrip | grep -F -q '"pictureRef":"ui:Picture"'
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
	$(BIN) raw-deflate-selftest | grep -q '"byteEqual":true'
	$(BIN) form-payload-structure-selftest | grep -q '"guidHeadNodes":2'
	$(BIN) form-payload-structure-selftest | grep -q '"candidateObjectNodes":1'
	$(BIN) form-payload-structure-selftest | grep -q '"knownDescriptorCandidateNodes":1'
	$(BIN) form-payload-structure-selftest | grep -F -q '"descriptorStatusFrequency":[{"status":"platform-resource-backed","count":2}]'
	$(BIN) form-payload-structure-selftest | grep -F -q '"descriptorBinding":{"status":"platform-resource-backed"'
	$(BIN) form-payload-structure-selftest | grep -F -q '"knownDescriptorPoolMember":true'
	$(BIN) form-payload-structure-selftest | grep -F -q '"symbol":"cf_form_controls8"'
	$(BIN) form-payload-structure-selftest | grep -F -q '"symbol":"cf_form_controls_position8"'
	$(BIN) form-payload-structure-selftest | grep -F -q '"symbol":"cf_form_controls_info8"'
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"knownDescriptorHitsTotal":12'; fi
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"nearestName":"GroupsCommandBar"'; fi
	if [ -f ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd | grep -F -q '"targetNamespace":"http://v8.1c.ru/8.1/data/ui"'; fi
	if [ -f ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd | grep -F -q '"formRelatedTypes"'; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -q '"09ccdc77-ea1a-4a6d-ab1c-3435eada2433"'; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -F -q '"repeatedGuidBlocks":["0x42370"'; fi

clean:
	rm -rf $(BUILD_DIR)
