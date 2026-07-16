find_package(ZLIB REQUIRED)

add_executable(oof-native src/main.cpp)

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
  find_program(OOF_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

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
