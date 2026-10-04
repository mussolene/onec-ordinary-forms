find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)

function(oof_enable_project_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(
  liboof
  STATIC
    src/form_bin.cpp
    src/model/metamodel.cpp
    src/model/ordinary_form.cpp
    src/source/form_xml.cpp
    src/source/schema_generator.cpp
    src/storage/form_stream.cpp
    src/storage/value_codec.cpp
)
add_library(oof::liboof ALIAS liboof)

set_target_properties(
  liboof
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME oof
)

target_include_directories(
  liboof
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
oof_enable_project_warnings(liboof)
target_link_libraries(liboof PRIVATE LibXml2::LibXml2 ZLIB::ZLIB)

add_executable(oof src/cli/main.cpp)
set_target_properties(
  oof
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(oof PRIVATE oof::liboof)
oof_enable_project_warnings(oof)

if(BUILD_TESTING)
  add_executable(oof-model-test tests/model_test.cpp)
  set_target_properties(
    oof-model-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-model-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-model-test)
  add_test(NAME product.model COMMAND oof-model-test)
  set_tests_properties(product.model PROPERTIES LABELS unit)

  add_executable(oof-result-test tests/result_test.cpp)
  set_target_properties(
    oof-result-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-result-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-result-test)
  add_test(NAME product.result COMMAND oof-result-test)
  set_tests_properties(product.result PROPERTIES LABELS unit)

  add_executable(oof-container-test tests/container_test.cpp)
  set_target_properties(
    oof-container-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-container-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-container-test)
  add_test(NAME product.container COMMAND oof-container-test)
  set_tests_properties(product.container PROPERTIES LABELS unit)

  add_executable(oof-list-stream-test tests/list_stream_test.cpp)
  set_target_properties(
    oof-list-stream-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-list-stream-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-list-stream-test)
  add_test(NAME product.list-stream COMMAND oof-list-stream-test)
  set_tests_properties(product.list-stream PROPERTIES LABELS unit)

  add_executable(oof-value-codec-test tests/value_codec_test.cpp)
  set_target_properties(
    oof-value-codec-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-value-codec-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-value-codec-test)
  add_test(NAME product.value-codec COMMAND oof-value-codec-test)
  set_tests_properties(product.value-codec PROPERTIES LABELS unit)

  add_executable(oof-form-stream-test tests/form_stream_test.cpp)
  set_target_properties(
    oof-form-stream-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-form-stream-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-form-stream-test)
  add_test(NAME product.form-stream COMMAND oof-form-stream-test)
  set_tests_properties(product.form-stream PROPERTIES LABELS unit)

  add_executable(oof-form-bin-test tests/form_bin_test.cpp)
  set_target_properties(
    oof-form-bin-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-form-bin-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-form-bin-test)
  add_test(NAME product.form-bin COMMAND oof-form-bin-test)
  set_tests_properties(product.form-bin PROPERTIES LABELS "unit;integration")

  add_executable(oof-schema-generator-test tests/schema_generator_test.cpp)
  set_target_properties(
    oof-schema-generator-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(
    oof-schema-generator-test
    PRIVATE
      oof::liboof
      LibXml2::LibXml2
  )
  oof_enable_project_warnings(oof-schema-generator-test)
  add_test(NAME product.schema-generator COMMAND oof-schema-generator-test)
  set_tests_properties(product.schema-generator PROPERTIES LABELS unit)

  add_executable(oof-form-xml-test tests/form_xml_test.cpp)
  set_target_properties(
    oof-form-xml-test
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_STANDARD 20
      CXX_STANDARD_REQUIRED ON
  )
  target_link_libraries(oof-form-xml-test PRIVATE oof::liboof)
  oof_enable_project_warnings(oof-form-xml-test)
  add_test(NAME product.form-xml COMMAND oof-form-xml-test)
  set_tests_properties(product.form-xml PROPERTIES LABELS unit)

  add_test(NAME product.cli.version COMMAND oof --version)
  set_tests_properties(
    product.cli.version
    PROPERTIES
      LABELS smoke
      PASS_REGULAR_EXPRESSION "oof 1.0.0-dev"
  )

  add_test(
    NAME product.cli.integration
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOOF_CLI=$<TARGET_FILE:oof>"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake"
  )
  set_tests_properties(product.cli.integration PROPERTIES LABELS "smoke;integration")

endif()
