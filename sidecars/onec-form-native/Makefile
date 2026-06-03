CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lz

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/platform_mechanism.hpp src/platform_list_stream.hpp src/platform_value.hpp src/platform_value_metadata.hpp src/platform_descriptor_registry.hpp src/platform_form_descriptor_join.hpp src/platform_form_schema.hpp src/platform_object_model.hpp src/platform_object_schema.hpp src/platform_property_registry.hpp src/platform_runtime_binding.hpp src/ordinary_controls.hpp src/ordinary_form_graph.hpp src/form_bin_container.hpp src/platform_guid_registry.hpp

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
	$(BIN) platform-form-schema | grep -F -q '"controlCount":27'
	$(BIN) platform-form-schema | grep -F -q '"directGuidBindings":0'
	$(BIN) platform-form-schema | grep -F -q '"guidBindingStatus":"not-present-in-xsd; use resource/binary evidence"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"TextBox"'
	$(BIN) platform-form-schema | grep -F -q '"streamElement":"txt"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"Separator"'
	$(BIN) platform-form-schema | grep -F -q '"streamElement":"sep"'
	$(BIN) platform-form-schema | grep -F -q '"valueTypes":"Value,ui:Picture,v81:UUID,ui:Color,ui:Font,ui:ShortCutType,ContextMenu"'
	$(BIN) platform-object-schema | grep -F -q '"source":"platform object schema join: mngcore logform_layouter XSD + shcntx API catalog + localization resource evidence"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"Button"'
	$(BIN) platform-object-schema | grep -F -q '"name":"TextColor","streamName":"txtClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"BackColor","streamName":"bkClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"BorderColor","streamName":"brdClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Font","streamName":"fnt","valueType":"ui:Font"'
	$(BIN) platform-object-schema | grep -F -q '"name":"TextColor","streamName":"txtClr","valueType":"ui:Color","defaultValue":"Color(Color::eV8Color, IV8Style::eButtonTextColor)","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Picture","streamName":"pic","valueType":"ui:Picture","defaultValue":"V8Picture()","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Font","streamName":"fnt","valueType":"ui:Font","defaultValue":"Font(Font::eAutoFont, 0)","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"defBtn","streamName":"defBtn","valueType":"attribute","defaultValue":"false","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"apiProperties":["Caption","Enabled","Font","Picture","Shortcut","ToolTip","Visible"]'
	$(BIN) platform-descriptor-join | grep -F -q '"bindingCount":25'
	$(BIN) platform-descriptor-join | grep -F -q '"schemaBackedCount":23'
	$(BIN) platform-descriptor-join | grep -F -q '"directRegistryCount":7'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"CheckBox"'
	$(BIN) platform-descriptor-join | grep -F -q '"fieldKindSymbol":"IDS_FIELDKIND_CHECKBOX"'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"RadioButton"'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"PivotChart"'
	$(BIN) platform-descriptor-join | grep -F -q '"status":"corpus-unbound-platform-extension"'
	$(BIN) platform-runtime-bindings | grep -F -q '"name":"script-context-dispatch"'
	$(BIN) platform-runtime-bindings | grep -F -q 'core::ContextCore::getPropVal/setPropVal/call'
	$(BIN) platform-runtime-bindings | grep -F -q 'LogFormButtonCustomizer'
	$(BIN) platform-runtime-bindings | grep -F -q '"name":"FormAttributes"'
	$(BIN) platform-runtime-bindings | grep -F -q '"name":"FormCommand"'
	$(BIN) platform-runtime-bindings | grep -F -q '"name":"FormEvent"'
	$(BIN) platform-runtime-bindings | grep -F -q '"name":"PivotChart"'
	$(BIN) platform-runtime-bindings | grep -F -q '"sampleMethods":"GetPicture,Refresh"'
	$(BIN) platform-runtime-bindings | grep -F -q 'chartui_ru.res/frntend_ru.res'
	$(BIN) platform-property-registry | grep -F -q '"source":"PlatformPropertyDescriptorRegistry"'
	$(BIN) platform-property-registry | grep -F -q '"descriptorCount":42'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"name-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"position-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"binding-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"event-action-record"'
	$(BIN) platform-property-registry | grep -F -q '"name":"Events"'
	$(BIN) platform-property-registry | grep -F -q '"source":"mngcore logform.xsd Command + cmi.xsd CommandInfo"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"color-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"font-record"'
	$(BIN) platform-property-registry | grep -F -q '"writableCount":33'
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
	$(BIN) formbin-xml-build-selftest | grep -F -q '"nameRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"titleRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"positionRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"bindingRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"anchorTargetNameVisible":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"anchorBindingRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"dimensionBindingRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"dimensionRecordRoundtrip":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"modulePreserved":true'
	$(BIN) formbin-xml-build-selftest | grep -F -q '"noRawXml":true'
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
	$(BIN) form-object-graph-selftest | grep -F -q '"materializedItems":2'
	$(BIN) form-object-graph-selftest | grep -F -q '"namedItems":2'
	$(BIN) form-object-graph-selftest | grep -F -q '"schemaBackedItems":2'
	$(BIN) form-object-graph-selftest | grep -F -q '"name":"Button1"'
	$(BIN) form-object-graph-selftest | grep -F -q '"platformType":"Button"'
	$(BIN) form-object-graph-selftest | grep -F -q '"name":"Check1"'
	$(BIN) form-object-graph-selftest | grep -F -q '"platformType":"CheckBox"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,"Button1",4294967295,0,0,0},{},{},{}},{35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26,6,{14,"Check1",4294967295,0,0,0},{},{},{}}}}}' > $(BUILD_DIR)/runtime-form-selftest.txt
	$(BIN) runtime-form-object-graph $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"source":"RuntimeForm:payload"'
	$(BIN) runtime-form-object-graph $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"materializedItems":2'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<ChildItems>' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<Button name="Button1" id="5"/>' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<Picture>' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<Font>' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<ListStream' $(BUILD_DIR)/runtime-form.xml
	perl -0pi -e 's/name="Button1"/name="ButtonXmlEdited"/' $(BUILD_DIR)/runtime-form.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form.xml $(BUILD_DIR)/runtime-form-xml-edited.txt | grep -F -q '"nameEdits":2'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-xml-edited.txt $(BUILD_DIR)/runtime-form-xml-redump.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q 'ButtonXmlEdited' $(BUILD_DIR)/runtime-form-xml-redump.xml
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{"ru","Run"}}},{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},{14,"Button1",4294967295,0,0,0},{0}}}}}' > $(BUILD_DIR)/runtime-form-position-selftest.txt
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-form-position.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Position left="1" top="2" right="101" bottom="22"' $(BUILD_DIR)/runtime-form-position.xml
	grep -F -q '<Binding coordinate="left" value="0"/>' $(BUILD_DIR)/runtime-form-position.xml
	grep -F -q '<DimensionBinding dimension="width" value="0"/>' $(BUILD_DIR)/runtime-form-position.xml
	perl -0pi -e 's/left="1"/left="9"/; s/coordinate="left" value="0"/coordinate="left" value="21"/; s/dimension="width" value="0"/dimension="width" value="2"/' $(BUILD_DIR)/runtime-form-position.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-form-position.xml $(BUILD_DIR)/runtime-form-position-edited.txt | grep -F -q '"positionEdits":1'
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-form-position.xml $(BUILD_DIR)/runtime-form-position-edited-2.txt | grep -F -q '"bindingEdits":6'
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-form-position.xml $(BUILD_DIR)/runtime-form-position-edited-3.txt | grep -F -q '"dimensionBindingEdits":4'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-position-edited.txt $(BUILD_DIR)/runtime-form-position-redump.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Position left="9" top="2" right="101" bottom="22"' $(BUILD_DIR)/runtime-form-position-redump.xml
	grep -F -q '<Binding coordinate="left" value="21"/>' $(BUILD_DIR)/runtime-form-position-redump.xml
	grep -F -q '<DimensionBinding dimension="width" value="2"/>' $(BUILD_DIR)/runtime-form-position-redump.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-left-set.txt 5 Лево 11 | grep -F -q '"slotCodec":"position-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-left-set.txt $(BUILD_DIR)/runtime-platform-left-set.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Position left="11" top="2" right="101" bottom="22"' $(BUILD_DIR)/runtime-platform-left-set.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-width-set.txt 5 Width 120 | grep -F -q '"slotCodec":"position-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-width-set.txt $(BUILD_DIR)/runtime-platform-width-set.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Position left="1" top="2" right="121" bottom="22"' $(BUILD_DIR)/runtime-platform-width-set.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-binding-set.txt 5 Binding.left 21 | grep -F -q '"slotCodec":"binding-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-binding-set.txt $(BUILD_DIR)/runtime-platform-binding-set.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Binding coordinate="left" value="21"/>' $(BUILD_DIR)/runtime-platform-binding-set.xml
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"source":"RuntimeForm:PlatformObject"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"contextContract":{"platformEvidence":"core85 exports IContextDef/GroupContext/IContextExtImplBase getNProps,getPropName,findProp,isPropReadable,isPropWritable,getPropVal,setPropVal,call; mngcore logform.xsd declares Form/elements/command/property'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"implementedWritableSlotCodecs":["name-record","scalar-flag","position-record","binding-record","attribute-record","command-record","event-action-record"]'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"collections":[{"name":"Items"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"name":"Attributes","localizedName":"Реквизиты"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"platformType":"Form"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"typeCategory":"core::kLogFormTypeInfoCategory"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q 'PlatformObjectTypeHandler<Form>'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"valueType":"FormItems"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"items":{"count":2'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"attributes":{"name":"Attributes","localizedName":"Реквизиты","valueType":"FormAttributes"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"commands":{"name":"Commands","localizedName":"Команды","valueType":"FormCommands"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"events":{"name":"Events","localizedName":"События","valueType":"FormEvents"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"slotCodec":"collection-record","readable":true,"writable":false,"count":0'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"methods":["Count","Find","Get","IndexOf"]'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"findFirstName":true'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"localizedName":"Заголовок"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"slotBinding":"platform-name-record:{14,name,...}"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"slotCodec":"name-record"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"writable":true'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Имя | grep -F -q '"operation":"getPropVal"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Имя | grep -F -q '"value":"Button1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"value":"V8Picture()"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"defaultValue":"V8Picture()"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"writePolicy":"omit-when-default"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"valueOrigin":"schema-default"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-position-selftest.txt 5 Заголовок | grep -F -q '"descriptorName":"Title"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-position-selftest.txt 5 Заголовок | grep -F -q '"value":"Run"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{{10,1,0,0,0,1,0,0,0,0,0,0,0},14,{1,{1,1,{"ru","Run"}}},1,1,0,0,0,0,{0,0,0},0,0,0,0,0,2},{}},{14,"ButtonWithBaseInfo",4294967295,0,0,0},{},{}}}}}' > $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt 5 Visible | grep -F -q '"value":"true"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt 5 Enabled | grep -F -q '"value":"true"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt $(BUILD_DIR)/runtime-form-baseinfo.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Visible>true</Visible>' $(BUILD_DIR)/runtime-form-baseinfo.xml
	grep -F -q '<Enabled>true</Enabled>' $(BUILD_DIR)/runtime-form-baseinfo.xml
	perl -0pi -e 's#<Visible>true</Visible>#<Visible>false</Visible>#; s#<Enabled>true</Enabled>#<Enabled>false</Enabled>#' $(BUILD_DIR)/runtime-form-baseinfo.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt $(BUILD_DIR)/runtime-form-baseinfo.xml $(BUILD_DIR)/runtime-form-baseinfo-edited.txt | grep -F -q '"scalarFlagEdits":2'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-edited.txt 5 Visible | grep -F -q '"value":"false"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-edited.txt 5 Enabled | grep -F -q '"value":"false"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18},{{1},17,{1,{{1},0,1,1,"Attr1",{"Pattern",{"#",7861785a-a6a1-485c-94c1-5f4a4e882abe}}}},{0}}}}' > $(BUILD_DIR)/runtime-form-attributes-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"attributes":{"name":"Attributes","localizedName":"Реквизиты","valueType":"FormAttributes"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"count":1'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"platformType":"FormAttribute"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-attributes-selftest.txt 'attribute:{1}' Name | grep -F -q '"value":"Attr1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-attributes-selftest.txt 'attribute:{1}' Type | grep -F -q '"value":"{\"Pattern\",{\"#\",7861785a-a6a1-485c-94c1-5f4a4e882abe}}"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-attributes-selftest.txt $(BUILD_DIR)/runtime-form-attributes.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Attributes>' $(BUILD_DIR)/runtime-form-attributes.xml
	grep -F -q '<Attribute name="Attr1" id="{1}" objectId="attribute:{1}" main="0" storedData="1">' $(BUILD_DIR)/runtime-form-attributes.xml
	cp $(BUILD_DIR)/runtime-form-attributes.xml $(BUILD_DIR)/runtime-form-attributes-edited.xml
	perl -0pi -e 's/name="Attr1"/name="AttrXmlEdited"/' $(BUILD_DIR)/runtime-form-attributes-edited.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-attributes-selftest.txt $(BUILD_DIR)/runtime-form-attributes-edited.xml $(BUILD_DIR)/runtime-form-attributes-edited.txt | grep -F -q '"attributeEdits":1'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-attributes-edited.txt 'attribute:{1}' Name | grep -F -q '"value":"AttrXmlEdited"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18},{command,{9},"RunCommand","RunCommandHandler",1},{{1},17,{1,{{1},0,1,1,"AttrAfterCommand",{"Pattern",{"#",7861785a-a6a1-485c-94c1-5f4a4e882abe}}}},{0}}}}' > $(BUILD_DIR)/runtime-form-command-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"commands":{"name":"Commands","localizedName":"Команды","valueType":"FormCommands"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"platformType":"FormCommand"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"name":"RunCommand"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'command:{9}' Handler | grep -F -q '"value":"RunCommandHandler"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'command:{9}' ModifiesData | grep -F -q '"value":"1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'attribute:{1}' Name | grep -F -q '"value":"AttrAfterCommand"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-command-selftest.txt $(BUILD_DIR)/runtime-form-command.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Command id="{9}" name="RunCommand" handler="RunCommandHandler" modifiesData="1" objectId="command:{9}"/>' $(BUILD_DIR)/runtime-form-command.xml
	cp $(BUILD_DIR)/runtime-form-command.xml $(BUILD_DIR)/runtime-form-command-edited.xml
	perl -0pi -e 's/name="RunCommand"/name="RunCommandXml"/; s/handler="RunCommandHandler"/handler="RunCommandHandlerXml"/; s/modifiesData="1"/modifiesData="0"/' $(BUILD_DIR)/runtime-form-command-edited.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-command-selftest.txt $(BUILD_DIR)/runtime-form-command-edited.xml $(BUILD_DIR)/runtime-form-command-edited.txt | grep -F -q '"commandEdits":3'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-edited.txt 'command:{9}' Name | grep -F -q '"value":"RunCommandXml"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-edited.txt 'command:{9}' Handler | grep -F -q '"value":"RunCommandHandlerXml"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-edited.txt 'command:{9}' ModifiesData | grep -F -q '"value":"0"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,"ButtonWithEvent",4294967295,0,0,0},{event,0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c,"ButtonWithEventClick"},{},{}}}}}' > $(BUILD_DIR)/runtime-form-event-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"events":{"name":"Events","localizedName":"События","valueType":"FormEvents"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"platformType":"FormEvent"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"name":"ButtonWithEventClick"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"value":"1"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"source":"mngcore logform.xsd Event"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-event-selftest.txt 'event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c' Handler | grep -F -q '"value":"ButtonWithEventClick"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-event-selftest.txt 'event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c' Parent | grep -F -q '"value":"5"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-event-selftest.txt $(BUILD_DIR)/runtime-form-event.xml | grep -F -q '"publicContract":"OrdinaryFormV2"'
	grep -F -q '<Events>' $(BUILD_DIR)/runtime-form-event.xml
	grep -F -q '<Event id="0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c" handler="ButtonWithEventClick" objectId="event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c" ownerId="5"/>' $(BUILD_DIR)/runtime-form-event.xml
	cp $(BUILD_DIR)/runtime-form-event.xml $(BUILD_DIR)/runtime-form-event-edited.xml
	perl -0pi -e 's/handler="ButtonWithEventClick"/handler="ButtonWithEventClickXml"/' $(BUILD_DIR)/runtime-form-event-edited.xml
	$(BIN) runtime-form-build-xml $(BUILD_DIR)/runtime-form-event-selftest.txt $(BUILD_DIR)/runtime-form-event-edited.xml $(BUILD_DIR)/runtime-form-event-edited.txt | grep -F -q '"eventEdits":1'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-event-edited.txt 'event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c' Handler | grep -F -q '"value":"ButtonWithEventClickXml"'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"canonicalRoundtripEqual":true'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"payloadRoundtripEqual":true'
	$(BIN) runtime-form-rebuild $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form-rebuilt.txt | grep -F -q '"schemaBackedItems":2'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-rebuilt.txt | grep -F -q '"canonicalRoundtripEqual":true'
	$(BIN) runtime-form-semantic-diff $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form-rebuilt.txt | grep -F -q '"normalizedEqual":true'
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-set.txt 5 Заголовок ButtonViaSet | grep -F -q '"operation":"setPropVal"'
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-set.txt 5 Заголовок ButtonViaSet | grep -F -q '"slotBinding":"platform-name-record:{14,name,...}"'
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-set.txt 5 Заголовок ButtonViaSet | grep -F -q '"descriptorName":"Title"'
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-set.txt 5 Заголовок ButtonViaSet | grep -F -q '"slotCodec":"name-record"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-platform-set.txt | grep -F -q '"name":"ButtonViaSet"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-platform-set.txt | grep -F -q '"value":"ButtonViaSet"'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-platform-set.txt | grep -F -q '"payloadRoundtripEqual":true'
	$(BIN) runtime-form-semantic-diff $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-set.txt | grep -F -q '"semanticDiffs":1'
	$(BIN) runtime-form-rename $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form-renamed.txt 5 ButtonRenamed | grep -F -q '"newName":"ButtonRenamed"'
	$(BIN) runtime-form-object-graph $(BUILD_DIR)/runtime-form-renamed.txt | grep -F -q '"name":"ButtonRenamed"'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-renamed.txt | grep -F -q '"payloadRoundtripEqual":true'
	$(BIN) runtime-form-semantic-diff $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form-renamed.txt | grep -F -q '"semanticDiffs":1'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"objectGraphReady":true'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"transferFormatAtomsEmbedded":true'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"directFacetLinkageRequiredForFormBinRebuild":true'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"controls8SectionDecoded":false'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"symbol":"cf_form_controls8"'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"recordSize":0'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"countSemantics":"payload byte size"'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"symbol":"cf_form_controls_position8"'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"recordSize":32'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"symbol":"cf_form_controls_info8"'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"recordSize":16'
	$(BIN) form-transfer-linkage-selftest | grep -F -q '"facetLinkage":"pending-transfer-section-decode"'
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"knownDescriptorHitsTotal":12'; fi
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"nearestName":"GroupsCommandBar"'; fi
	if [ -f ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd | grep -F -q '"targetNamespace":"http://v8.1c.ru/8.1/data/ui"'; fi
	if [ -f ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd | grep -F -q '"formRelatedTypes"'; fi
	if [ -f ../../work/platform85-xml-resources/chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd ]; then grep -F -q 'complexType name="PivotChart"' ../../work/platform85-xml-resources/chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -q '"09ccdc77-ea1a-4a6d-ab1c-3435eada2433"'; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -F -q '"repeatedGuidBlocks":["0x42370"'; fi

clean:
	rm -rf $(BUILD_DIR)
