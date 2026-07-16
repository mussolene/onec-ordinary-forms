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
    src/model/metamodel.cpp
    src/model/ordinary_form.cpp
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

add_executable(oof-native src/main.cpp)

target_include_directories(
  oof-native
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set_target_properties(
  oof-native
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(oof-native PRIVATE ZLIB::ZLIB)

# The unchanged migration source still uses the C++20 keyword as an identifier.
# Preload its dependencies before limiting this token remap to project code.
set(_oof_native_cxx20_compat_header "${CMAKE_CURRENT_BINARY_DIR}/cxx20-source-compat.hpp")
file(
  GENERATE
  OUTPUT "${_oof_native_cxx20_compat_header}"
  CONTENT [=[
#pragma once
#pragma push_macro("concept")
#undef concept
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <zlib.h>
#pragma pop_macro("concept")
]=]
)
target_compile_definitions(oof-native PRIVATE concept=ordinary_form_concept)

if(MSVC)
  target_compile_options(
    oof-native
    PRIVATE /W4 /permissive- "/FI${_oof_native_cxx20_compat_header}"
  )
else()
  target_compile_options(
    oof-native
    PRIVATE -Wall -Wextra -Wpedantic -include "${_oof_native_cxx20_compat_header}"
  )
endif()

option(
  OOF_NATIVE_MIGRATION_COMPAT
  "Copy oof-native to its pre-CMake path during the build-system migration"
  ON
)

if(OOF_NATIVE_MIGRATION_COMPAT)
  set(_oof_native_compat_dir "${CMAKE_CURRENT_SOURCE_DIR}/build")
  add_custom_target(
    oof-native-compat ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_oof_native_compat_dir}"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:oof-native>"
      "${_oof_native_compat_dir}/$<TARGET_FILE_NAME:oof-native>"
    DEPENDS oof-native
    COMMENT "Updating the migration-compatible oof-native path"
    VERBATIM
  )
endif()

if(BUILD_TESTING)
  find_package(LibXml2 REQUIRED)
  find_program(OOF_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

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

  add_test(NAME product.cli.version COMMAND oof --version)
  set_tests_properties(
    product.cli.version
    PROPERTIES
      LABELS smoke
      PASS_REGULAR_EXPRESSION "oof 1.0.0-dev"
  )

  add_test(
    NAME native.regression
    COMMAND
      "${OOF_MAKE_EXECUTABLE}"
      legacy-test
      "BIN=$<TARGET_FILE:oof-native>"
      "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/legacy-test"
      "LOCAL_PLATFORM_WORK=${CMAKE_CURRENT_BINARY_DIR}/no-local-platform-work"
  )
  set_tests_properties(
    native.regression
    PROPERTIES
      LABELS regression
      TIMEOUT 300
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )

  function(oof_add_smoke_test test_name command_name)
    add_test(NAME "${test_name}" COMMAND oof-native "${command_name}")
    set_tests_properties("${test_name}" PROPERTIES LABELS smoke)
  endfunction()

  oof_add_smoke_test(native.smoke.mechanism mechanism)
  oof_add_smoke_test(native.smoke.platform-object-schema platform-object-schema)

  oof_add_smoke_test(native.smoke.object-model-status object-model-gate)
  set_tests_properties(
    native.smoke.object-model-status
    PROPERTIES PASS_REGULAR_EXPRESSION "\"status\":\"PASS\""
  )

  oof_add_smoke_test(native.smoke.object-model-release-state object-model-gate)
  set_tests_properties(
    native.smoke.object-model-release-state
    PROPERTIES PASS_REGULAR_EXPRESSION "\"releaseReady\":false"
  )

  oof_add_smoke_test(native.smoke.object-model-coverage object-model-gate)
  set_tests_properties(
    native.smoke.object-model-coverage
    PROPERTIES PASS_REGULAR_EXPRESSION "\"coverageStatus\":\"PARTIAL\""
  )
endif()
