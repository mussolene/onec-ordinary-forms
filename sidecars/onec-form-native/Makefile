CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lz

BUILD_DIR := build
BIN := $(BUILD_DIR)/oof-native
SRC := src/main.cpp src/control_info_descriptor_registry.hpp src/ordinary_control_type_registry.hpp src/ordinary_form_concept_registry.hpp src/ordinary_form_object.hpp src/platform_mechanism.hpp src/platform_list_stream.hpp src/platform_value.hpp src/platform_value_metadata.hpp src/platform_descriptor_registry.hpp src/platform_form_descriptor_join.hpp src/platform_form_schema.hpp src/platform_object_model.hpp src/platform_object_schema.hpp src/platform_property_registry.hpp src/platform_runtime_binding.hpp src/ordinary_controls.hpp src/ordinary_form_graph.hpp src/form_bin_container.hpp src/platform_guid_registry.hpp

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

test: $(BIN)
	$(BIN) mechanism | grep -q '"cf_form_controls_info8":40192'
	$(BIN) mechanism | grep -q '"FormDataObject"'
	$(BIN) mechanism | grep -q '"TypeDomainPattern"'
	$(BIN) mechanism | grep -q '"core::LocalWString::serialize"'
	$(BIN) mechanism | grep -F -q '"sampleFormattedString":"{1,{1,1,{\"ru\",\"Title\"}},0}"'
	$(BIN) mechanism | grep -q '"schemaValueSurface"'
	$(BIN) mechanism | grep -F -q '"schemaSource":"xdto_root.res:data_ui.xsd Color"'
	$(BIN) mechanism | grep -F -q '"platformEvidence":"core82 Font::serialize/deserialize list arity 6; 8.5 schema adds scale/lineHeight"'
	$(BIN) mechanism | grep -F -q '"layoutStatus":"value-layer schema backed; ordinary object-slot semantics pending"'
	$(BIN) mechanism | grep -F -q '"platformEvidence":"core82/core85 V8Picture to_storage/from_storage entrypoints; Picture is base64/url/ref XSD value"'
	$(BIN) mechanism | grep -q '"ordinaryTransferRegistry"'
	$(BIN) mechanism | grep -q '"formatEntry":40'
	$(BIN) mechanism | grep -F -q '"countSemantics":"payload byte size"'
	$(BIN) mechanism | grep -F -q '"boundary":"existing IFile/HGLOBAL payload returned by FUN_00270da0"'
	$(BIN) platform-form-schema | grep -F -q '"source":"platform-resource:mngcore_root-168-http_v8.1c.ru_8.2_managed-application_logform.xsd:http://v8.1c.ru/8.2/managed-application/logform"'
	$(BIN) platform-form-schema | grep -F -q '"externalSchemaSource":"platform-resource:chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd:http://v8.1c.ru/8.2/data/chart"'
	$(BIN) platform-form-schema | grep -F -q '"controlCount":29'
	$(BIN) platform-form-schema | grep -F -q '"directGuidBindings":0'
	$(BIN) platform-form-schema | grep -F -q '"guidBindingStatus":"not-present-in-xsd; use resource/binary evidence"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"TextBox"'
	$(BIN) platform-form-schema | grep -F -q '"streamElement":"txt"'
	$(BIN) platform-form-schema | grep -F -q '"rootComplexType":"Field"'
	$(BIN) platform-form-schema | grep -F -q '"variantElement":"inputData"'
	$(BIN) platform-form-schema | grep -F -q '"variantComplexType":"InputFieldData"'
	$(BIN) platform-form-schema | grep -F -q 'txtClr->m_textColor'
	$(BIN) platform-form-schema | grep -F -q 'txtClr=kLogFormInputFieldDataDefTextColor'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"Separator"'
	$(BIN) platform-form-schema | grep -F -q '"streamElement":"sep"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"Chart"'
	$(BIN) platform-form-schema | grep -F -q '"variantElement":"chartData"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"PivotChart","streamElement":"","schemaSource":"platform-resource:chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd:http://v8.1c.ru/8.2/data/chart"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"Spreadsheet","streamElement":"sprdsht","schemaSource":"platform-resource:moxel_root-69-http_v8.1c.ru_8.2_data_spreadsheet.xsd:http://v8.1c.ru/8.2/data/spreadsheet + platform-resource:mngcore_root-168-http_v8.1c.ru_8.2_managed-application_logform.xsd:http://v8.1c.ru/8.2/managed-application/logform"'
	$(BIN) platform-form-schema | grep -F -q '"typeName":"ActiveXControl","streamElement":"ActiveXControl","schemaSource":"platform-resource:cf_form_controls_info8 + OrdinaryFormPalette.xsd + mngcore_root-171-http_v8.1c.ru_8.2_managed-application_core.xsd:http://v8.1c.ru/8.2/managed-application/logform/activex"'
	$(BIN) platform-object-schema | grep -F -q '"source":"platform object schema join: 8.5 mngcore logform XSD resource + shcntx API catalog + localization resource evidence"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"Form","xsdNamespace":"http://v8.1c.ru/8.2/managed-application/logform/layouter","streamElement":"Form","schemaSource":"platform-resource:mngcore_root-170-http_v8.1c.ru_8.2_managed-application_logform_layouter.xsd'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"FormAttribute","xsdNamespace":"http://v8.1c.ru/8.2/managed-application/logform","streamElement":"Property"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"TextBox","xsdNamespace":"http://v8.1c.ru/8.2/managed-application/logform","streamElement":"txt"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"PivotChart","xsdNamespace":"http://v8.1c.ru/8.2/data/chart","streamElement":""'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"Spreadsheet","xsdNamespace":"http://v8.1c.ru/8.2/managed-application/logform","streamElement":"sprdsht"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"ActiveXControl","xsdNamespace":"http://v8.1c.ru/8.2/managed-application/logform/activex","streamElement":"ActiveXControl"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Clsid","streamName":"Clsid","valueType":"core:UUID"'
	$(BIN) platform-object-schema | grep -F -q '"name":"RealHeight","streamName":"realHeight","valueType":"xs:decimal","defaultValue":"0","writePolicy":"omit-when-default","platformMember":"m_realHeight"'
	$(BIN) platform-object-schema | grep -F -q '"name":"EnterKeyBehavior","streamName":"enter","valueType":"tns:FormEnterKeyBehavior","defaultValue":"ControlNavigation","writePolicy":"omit-when-default","platformMember":"m_enterKeyBehavior"'
	$(BIN) platform-object-schema | grep -F -q '"name":"VerticalScrolling","streamName":"verScroll","valueType":"xs:boolean","defaultValue":"false","writePolicy":"omit-when-default","platformMember":"m_verticalScrolling"'
	$(BIN) platform-object-schema | grep -F -q '"name":"ConversationsRepresentation","streamName":"convRepr","valueType":"lf:LogFormShowConversations","defaultValue":"eLFSECShow","writePolicy":"omit-when-default","platformMember":"m_showECSButton"'
	$(BIN) platform-object-schema | grep -F -q '"typeName":"Button"'
	$(BIN) platform-object-schema | grep -F -q '"name":"TextColor","streamName":"txtClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"BackColor","streamName":"bkClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"BorderColor","streamName":"brdClr","valueType":"ui:Color"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Font","streamName":"fnt","valueType":"ui:Font"'
	$(BIN) platform-object-schema | grep -F -q '"rootComplexType":"Button"'
	$(BIN) platform-object-schema | grep -F -q '"platformMember":"m_textColor","platformDefault":"kLogFormButtonDefTextColor"'
	$(BIN) platform-object-schema | grep -F -q '"name":"TextColor","streamName":"txtClr","valueType":"ui:Color","defaultValue":"kLogFormButtonDefTextColor","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Picture","streamName":"pic","valueType":"ui:Picture","defaultValue":"V8Picture()","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"Picture","streamName":"pic","valueType":"ui:Picture","defaultValue":"V8Picture()","writePolicy":"omit-when-default","platformMember":"m_picture","platformDefault":"V8Picture()","slotBinding":"cf_form_controls_info8:pic","slotCodec":"picture-record","codecStatus":"pending-info8-codec","writable":false'
	$(BIN) platform-object-schema | grep -F -q '"name":"Font","streamName":"fnt","valueType":"ui:Font","defaultValue":"kLogFormButtonDefFont","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"name":"defButton","streamName":"defButton","valueType":"xs:boolean","defaultValue":"false","writePolicy":"omit-when-default"'
	$(BIN) platform-object-schema | grep -F -q '"apiProperties":["Caption","Enabled","Font","Picture","Shortcut","ToolTip","Visible"]'
	$(BIN) platform-descriptor-join | grep -F -q '"bindingCount":26'
	$(BIN) platform-descriptor-join | grep -F -q '"schemaBackedCount":26'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"PivotChart","streamElement":"","role":"ordinary-form pivot chart descriptor"'
	$(BIN) platform-descriptor-join | grep -F -q '"schemaBacked":true,"schema":{"source":"platform-resource:chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd:http://v8.1c.ru/8.2/data/chart","typeName":"PivotChart"'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"HTML","streamElement":"html","role":"ordinary-form HTML document field descriptor"'
	$(BIN) platform-descriptor-join | grep -F -q '"schemaBacked":true,"schema":{"source":"platform-resource:mngcore_root-168-http_v8.1c.ru_8.2_managed-application_logform.xsd:http://v8.1c.ru/8.2/managed-application/logform","typeName":"HTML","streamElement":"html"'
	$(BIN) platform-descriptor-join | grep -F -q '"platformType":"ActiveXControl","streamElement":"","role":"ordinary-form external control descriptor candidate"'
	$(BIN) platform-descriptor-join | grep -F -q '"schemaBacked":true,"schema":{"source":"platform-resource:cf_form_controls_info8 + OrdinaryFormPalette.xsd + mngcore_root-171-http_v8.1c.ru_8.2_managed-application_core.xsd:http://v8.1c.ru/8.2/managed-application/logform/activex","typeName":"ActiveXControl","streamElement":"ActiveXControl"'
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
	$(BIN) platform-property-registry | grep -F -q '"descriptorCount":405'
	$(BIN) platform-property-registry | grep -F -q '"baseDescriptorCount":47'
	$(BIN) platform-property-registry | grep -F -q '"generatedDescriptorCount":358'
	$(BIN) platform-property-registry | grep -F -q '"generatedSchemaDescriptorCount":339'
	$(BIN) platform-property-registry | grep -F -q '"generatedApiDescriptorCount":19'
	$(BIN) platform-property-registry | grep -F -q '"source":"platform-resource:chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd:http://v8.1c.ru/8.2/data/chart:PivotChart/horizontalScaleKeeping"'
	$(BIN) platform-property-registry | grep -F -q '"name":"pivotChartType","localizedName":"","valueType":"tns:PivotChartType"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"name-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"position-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"binding-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"control-info-slot"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"event-action-record"'
	$(BIN) platform-property-registry | grep -F -q '"name":"Clsid","localizedName":"CLSID","valueType":"UUID","slotCodec":"control-info-slot","slotBinding":"cf_form_controls_info8:ActiveXControl:Clsid","readable":true,"writable":true'
	$(BIN) platform-property-registry | grep -F -q '"name":"ReadOnly","localizedName":"ТолькоПросмотр","valueType":"Boolean","slotCodec":"control-info-slot","slotBinding":"cf_form_controls_info8:ReadOnly","readable":true,"writable":true'
	$(BIN) platform-property-registry | grep -F -q '"name":"State1","localizedName":"Состояние1","valueType":"ActiveXStateBlob","slotCodec":"control-info-slot","slotBinding":"cf_form_controls_info8:ActiveXControl:State1","readable":true,"writable":true'
	$(BIN) platform-property-registry | grep -F -q '"name":"State2","localizedName":"Состояние2","valueType":"ActiveXStateBlob","slotCodec":"control-info-slot","slotBinding":"cf_form_controls_info8:ActiveXControl:State2","readable":true,"writable":true'
	$(BIN) platform-property-registry | grep -F -q '"name":"AllowClose","localizedName":"","valueType":"Boolean","slotCodec":"none","slotBinding":"","readable":true,"writable":false,"source":"shcntx_root.hbk objects/catalog56/catalog246/Form","origin":"generated-platform-api-catalog"'
	$(BIN) platform-property-registry | grep -F -q '"name":"TextColor","localizedName":"","valueType":"ui:Color","slotCodec":"color-record","slotBinding":"cf_form_controls_info8:txtClr","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"name":"BackColor","localizedName":"","valueType":"ui:Color","slotCodec":"color-record","slotBinding":"cf_form_controls_info8:bkClr","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"name":"BorderColor","localizedName":"","valueType":"ui:Color","slotCodec":"color-record","slotBinding":"cf_form_controls_info8:brdClr","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"name":"ToolTip","localizedName":"","valueType":"xs:string","slotCodec":"none","slotBinding":"","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"origin":"generated-platform-schema-catalog"'
	$(BIN) platform-property-registry | grep -F -q '"name":"Events"'
	$(BIN) platform-property-registry | grep -F -q '"source":"mngcore logform.xsd Command + cmi.xsd CommandInfo"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"color-record"'
	$(BIN) platform-property-registry | grep -F -q '"slotCodec":"font-record"'
	$(BIN) platform-property-registry | grep -F -q '"name":"Color","localizedName":"Цвет","valueType":"Color","slotCodec":"color-record","slotBinding":"PlatformObjectSchemaMember.slotBinding","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"name":"Font","localizedName":"Шрифт","valueType":"Font","slotCodec":"font-record","slotBinding":"PlatformObjectSchemaMember.slotBinding","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"name":"Picture","localizedName":"Картинка","valueType":"Picture","slotCodec":"picture-record","slotBinding":"PlatformObjectSchemaMember.slotBinding","readable":true,"writable":false'
	$(BIN) platform-property-registry | grep -F -q '"writableCount":33'
	$(BIN) ordinary-form-concepts | grep -F -q '"source":"OrdinaryFormConceptRegistry"'
	$(BIN) ordinary-form-concepts | grep -F -q '"owner":"OrdinaryForm object model"'
	$(BIN) ordinary-form-concepts | grep -F -q '"decisionBoundary":"sources are evidence adapters; accepted concepts own public XML and serializer admission"'
	$(BIN) ordinary-form-concepts | grep -F -q '"controlConceptCount":26'
	$(BIN) ordinary-form-concepts | grep -F -q '"publicName":"InputField","apiName":"TextBox","runtimeIdentity":"381ed624-9217-4e63-85db-c4c3cb87daae","storageBinding":"txt"'
	$(BIN) platform-control-info-descriptors | grep -F -q '"descriptorCount":27'
	$(BIN) platform-control-info-descriptors | grep -F -q '"writerDescriptorControls":26'
	$(BIN) platform-control-info-descriptors | grep -F -q '"writablePromotions":0'
	$(BIN) platform-control-info-descriptors | grep -F -q '"controlType":"Panel","infoKind":"1"'
	$(BIN) platform-control-info-descriptors | grep -F -q '"name":"PagePositions","index":46'
	$(BIN) platform-control-info-descriptors | grep -F -q '"controlType":"Table","infoKind":"5"'
	$(BIN) platform-control-info-descriptors | grep -F -q '"name":"AutoMarkIncomplete","index":22'
	$(BIN) object-model-gate | grep -F -q '"controlInfoDescriptorCount":27'
	$(BIN) object-model-gate | grep -F -q '"conceptOwner":"OrdinaryFormConceptRegistry"'
	$(BIN) object-model-gate | grep -F -q '"acceptedConceptIncompleteRows":0'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaEscapeStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaStableObjectTypesStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaInputFieldPropertyTypesStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"controlInfoWritablePromotions":0'
	$(BIN) object-model-gate | grep -F -q '"releaseReady":false'
	$(BIN) object-model-gate | grep -F -q '"coverageStatus":"PARTIAL"'
	$(BIN) xsd-order-object-gate | grep -F -q '"status":"PASS"'
	$(BIN) xsd-order-object-gate | grep -F -q '"concept":"platform XSD sequence/choice/attributes -> descriptor slots -> PlatformFormObject"'
	$(BIN) xsd-order-object-roundtrip | grep -F -q '"status":"PASS"'
	$(BIN) xsd-order-object-roundtrip | grep -F -q '"xsdOrderPass":true'
	$(BIN) xsd-order-object-roundtrip | grep -F -q '"payloadStableAfterSecondWrite":true'
	$(BIN) object-graph-concept-selftest | grep -F -q '"status":"PASS"'
	$(BIN) object-graph-concept-selftest | grep -F -q '"concept":"bracket -> PlatformFormObject -> bracket"'
	$(BIN) object-graph-concept-selftest | grep -F -q '"sourcePackageUsed":false'
	$(BIN) object-graph-concept-selftest | grep -F -q '"platformValidationUsed":false'
	$(BIN) value-roundtrip | grep -F -q '"serialized":"{1,{1,2,{\"ru\",\"Title\"},{\"en\",\"Caption\"}},1}"'
	$(BIN) value-roundtrip | grep -q '"itemCount":2'
	$(BIN) value-roundtrip | grep -F -q '"compositeID":"{42,09ccdc77-ea1a-4a6d-ab1c-3435eada2433}"'
	$(BIN) value-roundtrip | grep -F -q '"typeDomainPattern":"{\"Pattern\",{\"T\",d47d59f8-73f0-481c-8b5e-f6384c0a4804}}"'
	$(BIN) value-roundtrip | grep -F -q '"genericValue":"{\"String\",\"Value\"}"'
	$(BIN) value-roundtrip | grep -F -q '"colorSchema":"#123456"'
	$(BIN) value-roundtrip | grep -F -q '"fontFace":"Arial"'
	$(BIN) value-roundtrip | grep -F -q '"borderStyle":"Single"'
	$(BIN) value-roundtrip | grep -F -q '"pictureRef":"ui:Picture"'
	$(BIN) formbin-selftest | grep -q '"byteEqual":true'
	$(BIN) formbin-selftest | grep -q '"firstName":"form"'
	$(BIN) formbin-source-package-selftest | grep -F -q '"containerTicksSource":"file-mtime"'
	$(BIN) formbin-source-package-selftest | grep -F -q '"formTicksMatch":true'
	$(BIN) formbin-source-package-selftest | grep -F -q '"moduleTicksMatch":true'
	$(BIN) formbin-source-package-selftest | grep -F -q '"modulePayloadMatch":true'
	$(BIN) formbin-source-package-selftest | grep -F -q '"noZeroTicks":true'
	$(BIN) formbin-package-selftest | grep -F -q '"nameRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"titleRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"positionRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"bindingRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"anchorTargetNameVisible":true'
	$(BIN) formbin-package-selftest | grep -F -q '"anchorBindingRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"dimensionBindingRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"dimensionRecordRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlToPlatformFormObject":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectItems":1'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectNestedItems":2'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectNameRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectTitleRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectPositionRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectBindingRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectDimensionBindingRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectChildItemsRoundtrip":true'
	$(BIN) formbin-package-selftest | grep -F -q '"xmlObjectNoRawXml":true'
	$(BIN) formbin-package-selftest | grep -F -q '"modulePreserved":true'
	$(BIN) formbin-package-selftest | grep -F -q '"noRawXml":true'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"source":"Form.bin:PlatformObject"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"pictureValue":"V8Picture()"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"picturePlatformMember":"m_picture"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"pictureObjectClass":"Picture"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"pictureConstructor":"New Picture"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"pictureStorage":"empty"'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"explicitPictureXml":true'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"explicitColorXml":true'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"explicitFontXml":true'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"titleRoundtrip":true'
	$(BIN) formbin-platform-object-selftest | grep -F -q '"modulePreserved":true'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"object":"OrdinaryForm"'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"conceptOwner":"OrdinaryFormConceptRegistry"'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"itemsCount":1'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"afterTitle":"ButtonViaOrdinaryForm"'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"afterVisible":"false"'
	$(BIN) ordinary-form-object-selftest | grep -F -q '"diagnosticPropertyRejected":true'
	$(BIN) empty-form-object-roundtrip "ПустаяФорма" | grep -F -q '"path":"PlatformFormObject -> bracket -> PlatformFormObject -> bracket"'
	$(BIN) empty-form-object-roundtrip "ПустаяФорма" | grep -F -q '"writtenObjectCounts":{"items":0,"attributes":0,"commands":0,"events":0,"edges":0}'
	$(BIN) empty-form-object-roundtrip "ПустаяФорма" | grep -F -q '"objectSignatureStableAfterSecondWrite":true'
	$(BIN) empty-form-object-roundtrip "ПустаяФорма" | grep -F -q '"payloadStableAfterSecondWrite":true'
	$(BIN) raw-deflate-selftest | grep -q '"byteEqual":true'
	$(BIN) raw-deflate-selftest | grep -q '"reinflateEqual":true'
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
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-form.xml | grep -F -q '"source":"RuntimeForm:OrdinaryForm"'
	grep -F -q '<ChildItems>' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<Button name="Button1" id="5">' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<CheckBox name="Check1" id="6">' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<Visible>true</Visible>' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<Enabled>true</Enabled>' $(BUILD_DIR)/runtime-form.xml
	grep -F -q '<Position left="0" top="0" right="0" bottom="0">' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<Picture>' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<Font>' $(BUILD_DIR)/runtime-form.xml
	! grep -F -q '<ListStream' $(BUILD_DIR)/runtime-form.xml
	$(BIN) runtime-form-dump-platform-xsd-xml $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/runtime-platform-xsd.xml | grep -F -q '"xmlVocabulary":"platform-xsd-stream-elements"'
	grep -F -q '<Form xmlns="http://v8.1c.ru/8.2/managed-application/logform/layouter"' $(BUILD_DIR)/runtime-platform-xsd.xml
	grep -F -q '<btn xmlns="http://v8.1c.ru/8.2/managed-application/logform" id="5" name="Button1"' $(BUILD_DIR)/runtime-platform-xsd.xml
	grep -F -q '<chk xmlns="http://v8.1c.ru/8.2/managed-application/logform" id="6" name="Check1"' $(BUILD_DIR)/runtime-platform-xsd.xml
	! grep -F -q '<Object' $(BUILD_DIR)/runtime-platform-xsd.xml
	! grep -F -q '<Property name=' $(BUILD_DIR)/runtime-platform-xsd.xml
	$(BIN) platform-xsd-xml-object $(BUILD_DIR)/runtime-platform-xsd.xml > $(BUILD_DIR)/runtime-platform-xsd-object.json
	grep -F -q '"source":"platform-XSD-XML"' $(BUILD_DIR)/runtime-platform-xsd-object.json
	grep -F -q '"typeSource":"platform XDTO IObject + PlatformObjectSchema"' $(BUILD_DIR)/runtime-platform-xsd-object.json
	grep -F -q '"items":{"count":2' $(BUILD_DIR)/runtime-platform-xsd-object.json
	grep -F -q '"objectId":"5","name":"Button1","platformType":"Button"' $(BUILD_DIR)/runtime-platform-xsd-object.json
	grep -F -q '"objectId":"6","name":"Check1","platformType":"CheckBox"' $(BUILD_DIR)/runtime-platform-xsd-object.json
	! grep -F -q '"commands":{"count":1' $(BUILD_DIR)/runtime-platform-xsd-object.json
	$(BIN) platform-xsd-xml-roundtrip $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-redump.xml | grep -F -q '"path":"platform-XSD-XML -> PlatformXdtoObject -> PlatformFormObject -> PlatformXdtoObject -> platform-XSD-XML"'
	cmp $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-redump.xml
	printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' '<Form xmlns="http://v8.1c.ru/8.2/managed-application/logform/layouter">' '  <btn xmlns="http://v8.1c.ru/8.2/managed-application/logform" id="5" name="Button1"><title>Wrong order</title><command></command></btn>' '</Form>' > $(BUILD_DIR)/runtime-platform-xsd-invalid-order.xml
	! $(BIN) platform-xsd-xml-object $(BUILD_DIR)/runtime-platform-xsd-invalid-order.xml
	$(BIN) platform-xsd-xml-build-runtime $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-built.txt | grep -F -q '"path":"platform-XSD-XML -> PlatformXdtoObject -> XSD schema-order ListOutStream -> bracket"'
	$(BIN) platform-xsd-xml-build-runtime $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-built.txt | grep -F -q '"schemaOrderWriter":"PlatformXdtoObject"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-platform-xsd-built.txt > $(BUILD_DIR)/runtime-platform-xsd-built-object.json
	grep -F -q '"items":{"count":2' $(BUILD_DIR)/runtime-platform-xsd-built-object.json
	grep -F -q '"name":"Button1","platformType":"Button"' $(BUILD_DIR)/runtime-platform-xsd-built-object.json
	grep -F -q '"name":"Check1","platformType":"CheckBox"' $(BUILD_DIR)/runtime-platform-xsd-built-object.json
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/runtime-platform-xsd-built.txt | grep -F -q '"payloadStableAfterSecondWrite":true'
	$(BIN) platform-xsd-xml-build-formbin $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-built-Form.bin | grep -F -q '"path":"platform-XSD-XML -> PlatformXdtoObject -> XSD schema-order ListOutStream -> Form.bin"'
	$(BIN) platform-xsd-xml-build-formbin $(BUILD_DIR)/runtime-platform-xsd.xml $(BUILD_DIR)/runtime-platform-xsd-built-Form.bin | grep -F -q '"schemaOrderWriter":"PlatformXdtoObject"'
	$(BIN) formbin-platform-object $(BUILD_DIR)/runtime-platform-xsd-built-Form.bin > $(BUILD_DIR)/runtime-platform-xsd-built-formbin-object.json
	grep -F -q '"items":{"count":2' $(BUILD_DIR)/runtime-platform-xsd-built-formbin-object.json
	grep -F -q '"name":"Button1","platformType":"Button"' $(BUILD_DIR)/runtime-platform-xsd-built-formbin-object.json
	grep -F -q '"name":"Check1","platformType":"CheckBox"' $(BUILD_DIR)/runtime-platform-xsd-built-formbin-object.json
	$(BIN) formbin-xsd-order-object-gate $(BUILD_DIR)/runtime-platform-xsd-built-Form.bin | grep -F -q '"status":"PASS"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{"ru","Run"}}},{8,1,2,101,22,0,0,0,0,0,0,0,0,0,0,0,0},{14,"Button1",4294967295,0,0,0},{0}}}}}' > $(BUILD_DIR)/runtime-form-position-selftest.txt
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-form-position.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Position left="1" top="2" right="101" bottom="22"' $(BUILD_DIR)/runtime-form-position.xml
	grep -F -q '<Binding coordinate="left" value="0"/>' $(BUILD_DIR)/runtime-form-position.xml
	grep -F -q '<DimensionBinding dimension="width" value="0"/>' $(BUILD_DIR)/runtime-form-position.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-left-set.txt 5 Лево 11 | grep -F -q '"slotCodec":"position-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-left-set.txt $(BUILD_DIR)/runtime-platform-left-set.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Position left="11" top="2" right="101" bottom="22"' $(BUILD_DIR)/runtime-platform-left-set.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-width-set.txt 5 Width 120 | grep -F -q '"slotCodec":"position-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-width-set.txt $(BUILD_DIR)/runtime-platform-width-set.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Position left="1" top="2" right="121" bottom="22"' $(BUILD_DIR)/runtime-platform-width-set.xml
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-position-selftest.txt $(BUILD_DIR)/runtime-platform-binding-set.txt 5 Binding.left 21 | grep -F -q '"slotCodec":"binding-record"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-platform-binding-set.txt $(BUILD_DIR)/runtime-platform-binding-set.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Binding coordinate="left" value="21"/>' $(BUILD_DIR)/runtime-platform-binding-set.xml
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{1,1,{"ru","Run"}}},{8,1,2,101,22,0,0,0,0,0,0,{8,1,1,{"ru","Русский"},{128,72}},0,0,0,0,0},{14,"ButtonWithInnerPayload",4294967295,0,0,0},{0}}}}}' > $(BUILD_DIR)/runtime-form-inner-payload-selftest.txt
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-inner-payload-selftest.txt $(BUILD_DIR)/runtime-form-inner-payload.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Binding coordinate="left" value="0"/>' $(BUILD_DIR)/runtime-form-inner-payload.xml
	grep -F -q '<Binding coordinate="horizontalCenter" value="0"/>' $(BUILD_DIR)/runtime-form-inner-payload.xml
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-inner-payload-selftest.txt | grep -F -q '"payloadRoundtripEqual":true'
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
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"name":"AllowClose","localizedName":"","valueType":"Boolean"'
	! $(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"descriptorCoverageGaps":[{"name":"AllowClose"'
	! $(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"reason":"api property has no PlatformPropertyDescriptor binding"'
	! $(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"valueType":"GenericValue"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 RealHeight | grep -F -q '"value":"0"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 RealHeight | grep -F -q '"slotBinding":"logform_layouter.xsd:Form@realHeight"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 EnterKeyBehavior | grep -F -q '"value":"ControlNavigation"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 EnterKeyBehavior | grep -F -q '"platformMember":"m_enterKeyBehavior"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 VerticalScrolling | grep -F -q '"value":"false"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 0 ConversationsRepresentation | grep -F -q '"value":"eLFSECShow"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Имя | grep -F -q '"operation":"getPropVal"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Имя | grep -F -q '"value":"Button1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"value":"V8Picture()"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"defaultValue":"V8Picture()"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"writePolicy":"omit-when-default"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"valueOrigin":"schema-default"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"platformMember":"m_picture"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"platformDefault":"V8Picture()"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"slotBinding":"cf_form_controls_info8:pic"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"slotCodec":"picture-record"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"writable":false'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"valueObject":{"class":"Picture","constructor":"New Picture","storage":"empty","literal":"V8Picture()","schemaValue":"","listStream":"{{\"none\"},\"\"}","ownerMember":"m_picture"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Picture | grep -F -q '"evidence":"xdto_root.res:data_ui.xsd Picture/PictureRef + core::V8Picture::to_storage/from_storage"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 TextColor | grep -F -q '"valueObject":{"class":"Color","constructor":"New Color"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 TextColor | grep -F -q '"storage":"style","literal":"kLogFormButtonDefTextColor","schemaValue":"style:ButtonTextColor"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 TextColor | grep -F -q '"ownerMember":"m_textColor","evidence":"xdto_root.res:data_ui.xsd Color + core::Color::serialize/deserialize"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Font | grep -F -q '"valueObject":{"class":"Font","constructor":"New Font"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Font | grep -F -q '"storage":"style","literal":"kLogFormButtonDefFont","schemaValue":"sys:DefaultGUIFont"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-selftest.txt 5 Font | grep -F -q '"ownerMember":"m_font","evidence":"xdto_root.res:data_ui.xsd Font + core::Font::serialize/deserialize"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-position-selftest.txt 5 Заголовок | grep -F -q '"descriptorName":"Title"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-position-selftest.txt 5 Заголовок | grep -F -q '"value":"Run"'
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{1,{{10,1,0,0,0,1,0,0,0,0,0,0,0},14,{1,{1,1,{"ru","Run"}}},1,1,0,0,0,0,{0,0,0},0,0,0,0,0,2},{}},{14,"ButtonWithBaseInfo",4294967295,0,0,0},{},{}}}}}' > $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt 5 Visible | grep -F -q '"value":"true"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt 5 Enabled | grep -F -q '"value":"true"'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-baseinfo-selftest.txt $(BUILD_DIR)/runtime-form-baseinfo.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Visible>true</Visible>' $(BUILD_DIR)/runtime-form-baseinfo.xml
	grep -F -q '<Enabled>true</Enabled>' $(BUILD_DIR)/runtime-form-baseinfo.xml
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18},{{1},17,{1,{{1},0,1,1,"Attr1",{"Pattern",{"#",7861785a-a6a1-485c-94c1-5f4a4e882abe}}}},{0}}}}' > $(BUILD_DIR)/runtime-form-attributes-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"attributes":{"name":"Attributes","localizedName":"Реквизиты","valueType":"FormAttributes"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"count":1'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"platformType":"FormAttribute"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-attributes-selftest.txt 'attribute:{1}' Name | grep -F -q '"value":"Attr1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-attributes-selftest.txt 'attribute:{1}' Type | grep -F -q '"value":"{\"Pattern\",{\"#\",7861785a-a6a1-485c-94c1-5f4a4e882abe}}"'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"attributeObjectsChecked":1'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-attributes-selftest.txt | grep -F -q '"collectionObjectsSkipped":0'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-attributes-selftest.txt $(BUILD_DIR)/runtime-form-attributes.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Attributes>' $(BUILD_DIR)/runtime-form-attributes.xml
	grep -F -q '<Attribute name="Attr1" id="{1}" objectId="attribute:{1}" main="0" storedData="1">' $(BUILD_DIR)/runtime-form-attributes.xml
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18},{command,{9},"RunCommand","RunCommandHandler",1},{{1},17,{1,{{1},0,1,1,"AttrAfterCommand",{"Pattern",{"#",7861785a-a6a1-485c-94c1-5f4a4e882abe}}}},{0}}}}' > $(BUILD_DIR)/runtime-form-command-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"commands":{"name":"Commands","localizedName":"Команды","valueType":"FormCommands"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"platformType":"FormCommand"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"name":"RunCommand"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'command:{9}:0' Handler | grep -F -q '"value":"RunCommandHandler"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'command:{9}:0' ModifiesData | grep -F -q '"value":"1"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-command-selftest.txt 'attribute:{1}' Name | grep -F -q '"value":"AttrAfterCommand"'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"attributeObjectsChecked":1'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"commandObjectsChecked":1'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-command-selftest.txt | grep -F -q '"collectionObjectsSkipped":0'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-command-selftest.txt $(BUILD_DIR)/runtime-form-command.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Command id="{9}" name="RunCommand" handler="RunCommandHandler" modifiesData="1" objectId="command:{9}:0"/>' $(BUILD_DIR)/runtime-form-command.xml
	printf '{"#",5c83cba4-7a20-4102-a5be-add0ee74f6a1,{27,{18,{6ff79819-710e-4145-97cd-1618da79e3e2,5,{14,"ButtonWithEvent",4294967295,0,0,0},{event,0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c,"ButtonWithEventClick"},{},{}}}}}' > $(BUILD_DIR)/runtime-form-event-selftest.txt
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"events":{"name":"Events","localizedName":"События","valueType":"FormEvents"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"platformType":"FormEvent"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"name":"ButtonWithEventClick"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"value":"1"'
	$(BIN) runtime-platform-object $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"source":"mngcore logform.xsd Event"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-event-selftest.txt 'event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c:0' Handler | grep -F -q '"value":"ButtonWithEventClick"'
	$(BIN) runtime-platform-object-get $(BUILD_DIR)/runtime-form-event-selftest.txt 'event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c:0' Parent | grep -F -q '"value":"5"'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"eventObjectsChecked":1'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-event-selftest.txt | grep -F -q '"collectionObjectsSkipped":0'
	$(BIN) runtime-form-dump-xml $(BUILD_DIR)/runtime-form-event-selftest.txt $(BUILD_DIR)/runtime-form-event.xml | grep -F -q '"publicContract":"OrdinaryForm"'
	grep -F -q '<Events>' $(BUILD_DIR)/runtime-form-event.xml
	grep -F -q '<Event id="0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c" handler="ButtonWithEventClick" objectId="event:5:0bd8d8ee-83f5-4b6f-bf0f-32cb6cdbcf8c:0" ownerId="5"/>' $(BUILD_DIR)/runtime-form-event.xml
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"canonicalRoundtripEqual":true'
	$(BIN) runtime-form-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"payloadRoundtripEqual":true'
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"path":"bracket -> PlatformFormObject -> bracket -> PlatformFormObject -> bracket"'
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"objectSignatureEqualAfterFirstWrite":true'
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"objectSignatureStableAfterSecondWrite":true'
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"payloadStableAfterSecondWrite":true'
	$(BIN) runtime-form-object-roundtrip-diff $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"firstWriteDiff"'
	$(BIN) runtime-form-object-roundtrip-diff $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"secondWriteDiffCounts":{"missingObjects":0'
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
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"status":"PASS"'
	$(BIN) runtime-xsd-order-object-gate $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"itemObjectsChecked":2'
	$(BIN) runtime-xsd-order-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"status":"PASS"'
	$(BIN) runtime-xsd-order-object-roundtrip $(BUILD_DIR)/runtime-form-selftest.txt | grep -F -q '"xsdOrderPass":true'
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"knownDescriptorHitsTotal":12'; fi
	if [ -f ../../work/platform82-container-libs/mngui_root.res ]; then $(BIN) platform-resource-descriptor-scan ../../work/platform82-container-libs/mngui_root.res | grep -F -q '"nearestName":"GroupsCommandBar"'; fi
	if [ -f ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/xdto_root-04-http_v8.1c.ru_8.1_data_ui.xsd | grep -F -q '"targetNamespace":"http://v8.1c.ru/8.1/data/ui"'; fi
	if [ -f ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd ]; then $(BIN) platform-xsd-inventory ../../work/platform82-xml-resources/mngcore_root-03-http_v8.1c.ru_8.2_managed-application_logform.xsd | grep -F -q '"formRelatedTypes"'; fi
	if [ -f ../../work/platform85-xml-resources/chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd ]; then grep -F -q 'complexType name="PivotChart"' ../../work/platform85-xml-resources/chart_root-01-http_v8.1c.ru_8.2_data_chart.xsd; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -q '"09ccdc77-ea1a-4a6d-ab1c-3435eada2433"'; fi
	if [ -f ../../work/platform85-container-libs/dsgnfrm.so ]; then $(BIN) platform-guid-scan ../../work/platform85-container-libs/dsgnfrm.so | grep -F -q '"repeatedGuidBlocks":["0x42370"'; fi
	$(BIN) runtime-platform-object-set $(BUILD_DIR)/runtime-form-selftest.txt $(BUILD_DIR)/dual-projection-object.txt 5 Имя DualProjectionName | grep -F -q '"operation":"setPropVal"'
	$(BIN) runtime-form-object-roundtrip $(BUILD_DIR)/dual-projection-object.txt | grep -F -q '"objectSignatureStableAfterSecondWrite":true'
	$(BIN) object-model-gate | grep -F -q '"status":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaEscapeStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaStableObjectTypesStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"publicSchemaInputFieldPropertyTypesStatus":"PASS"'
	$(BIN) object-model-gate | grep -F -q '"releaseReady":false'
	$(BIN) object-model-gate | grep -F -q '"coverageStatus":"PARTIAL"'
	total=0; ok=0; list_file="$(BUILD_DIR)/native-lossless-formbins.txt"; : > "$$list_file"; \
	for root in ../../work/oracle-runtime/runtime-source/root ../../work/oracle-runtime/blank-source/root; do \
		if [ -d "$$root" ]; then \
			find "$$root" -path '*/Forms/*/Ext/Form.bin' -type f >> "$$list_file"; \
		fi; \
	done; \
	sort "$$list_file" -o "$$list_file"; \
	while IFS= read -r form_bin; do \
		if [ -n "$$form_bin" ]; then \
			total=$$((total + 1)); \
			$(BIN) formbin-roundtrip "$$form_bin" | grep -F -q '"logicalEqual":true'; \
			$(BIN) formbin-xsd-order-object-gate "$$form_bin" | grep -F -q '"status":"PASS"'; \
			$(BIN) formbin-xsd-order-object-roundtrip "$$form_bin" | grep -F -q '"status":"PASS"'; \
			ok=$$((ok + 1)); \
		fi; \
	done < "$$list_file"; \
	printf 'native-lossless-roundtrip-gate: PASS (%s/%s payload-lossless)\n' "$$ok" "$$total"

clean:
	rm -rf $(BUILD_DIR)
