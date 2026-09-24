if(NOT DEFINED OOF_CLI OR OOF_CLI STREQUAL "")
  message(FATAL_ERROR "OOF_CLI must name the oof executable")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "TEST_ROOT must name the test's private temporary root")
endif()
set(CLI "${OOF_CLI}")
set(test_root "${TEST_ROOT}/product-cli-integration")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}/source/Form")

function(run_cli expected_result description)
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(NOT "${result}" STREQUAL "${expected_result}")
    message(FATAL_ERROR "${description}: expected exit ${expected_result}, got ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
  endif()
  set(CLI_STDOUT "${stdout}" PARENT_SCOPE)
endfunction()

function(require_contains value needle description)
  string(FIND "${value}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: expected to find '${needle}' in '${value}'")
  endif()
endfunction()

function(require_file_equals path expected description)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${description}: missing ${path}")
  endif()
  file(READ "${path}" actual)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "${description}: contents differ\nexpected: ${expected}\nactual: ${actual}")
  endif()
endfunction()

set(source_xml "${test_root}/source/Form.xml")
set(module_file "${test_root}/source/Form/Module.bsl")
set(first_bin "${test_root}/first.bin")
set(first_xml "${test_root}/first/Form.xml")
set(second_bin "${test_root}/second.bin")
set(second_xml "${test_root}/second/Form.xml")
set(module_text "procedure RunClick()\nendprocedure\n")
file(WRITE "${source_xml}" [=[
<Form id="1" name="Main" ordinaryFormVersion="2.1">
  <Caption>Before</Caption>
  <ChildItems>
    <Button name="Run" id="2">
      <Position><Top>20</Top><Visible>false</Visible><Height>30</Height><Left>10</Left><Width>100</Width></Position>
      <Enabled>false</Enabled>
      <Caption>Run</Caption>
      <Events><Click id="3">RunClick</Click></Events>
    </Button>
  </ChildItems>
</Form>
]=])
file(WRITE "${module_file}" "${module_text}")

# Source package -> Form.bin -> public XML and module sidecar.
run_cli(0 "initial build" build "${source_xml}" "${first_bin}" --json)
require_contains("${CLI_STDOUT}" "\"ok\":true" "initial build JSON result")
run_cli(0 "initial dump" dump "${first_bin}" "${first_xml}" --json)
require_contains("${CLI_STDOUT}" "\"ok\":true" "initial dump JSON result")
require_file_equals("${test_root}/first/Form/Module.bsl" "${module_text}" "initial module preservation")

# Edit a named public property, rebuild, and ensure it survives another dump.
file(READ "${first_xml}" xml_text)
string(REPLACE "<Caption>Before</Caption>" "<Caption>After</Caption>" edited_xml "${xml_text}")
string(REPLACE "<Caption>Run</Caption>" "<Caption>Launch</Caption>" edited_xml "${edited_xml}")
if(edited_xml STREQUAL xml_text)
  message(FATAL_ERROR "initial dump did not contain the expected named Caption property")
endif()
file(WRITE "${first_xml}" "${edited_xml}")
run_cli(0 "edited build" build "${first_xml}" "${second_bin}" --json)
run_cli(0 "edited dump" dump "${second_bin}" "${second_xml}" --json)
file(READ "${second_xml}" final_xml_text)
require_contains("${final_xml_text}" "<Caption>After</Caption>" "edited named Caption round-trip")
require_contains("${final_xml_text}" "<Caption>Launch</Caption>" "edited Button Caption round-trip")
require_file_equals("${test_root}/second/Form/Module.bsl" "${module_text}" "edited module preservation")

# Errors must be diagnostic, nonzero, and leave a pre-existing output untouched.
set(protected_output "${test_root}/protected.bin")
file(WRITE "${protected_output}" "preserve this output")
file(REMOVE "${module_file}")
run_cli(1 "missing Module.bsl" build "${source_xml}" "${protected_output}" --json)
require_contains("${CLI_STDOUT}" "OOF0005" "missing module diagnostic")
require_file_equals("${protected_output}" "preserve this output" "missing module must not overwrite output")

file(WRITE "${module_file}" "")
file(WRITE "${source_xml}" "<Form")
run_cli(1 "invalid XML" build "${source_xml}" "${protected_output}" --json)
require_contains("${CLI_STDOUT}" "diagnostics" "invalid XML diagnostics")
require_file_equals("${protected_output}" "preserve this output" "invalid XML must not overwrite output")

run_cli(64 "unknown validate" validate --json)
require_contains("${CLI_STDOUT}" "OOF0002" "unknown command diagnostic")
require_contains("${CLI_STDOUT}" "dump|build" "supported command list")
run_cli(64 "wrong arity" build only-one-operand --json)
require_contains("${CLI_STDOUT}" "OOF0003" "wrong arity diagnostic")
run_cli(0 "help" --help)
require_contains("${CLI_STDOUT}" "oof dump" "help dump command")
require_contains("${CLI_STDOUT}" "oof build" "help build command")
require_contains("${CLI_STDOUT}" "Module.bsl" "help module requirement")
foreach(removed_command validate diff edit)
  string(FIND "${CLI_STDOUT}" "${removed_command}" command_position)
  if(NOT command_position EQUAL -1)
    message(FATAL_ERROR "help must not advertise removed command ${removed_command}")
  endif()
endforeach()

# An intentionally empty module is still an explicit, valid sidecar.
set(empty_module_source "${test_root}/empty/Form.xml")
set(empty_module_bin "${test_root}/empty.bin")
file(MAKE_DIRECTORY "${test_root}/empty/Form")
file(WRITE "${empty_module_source}" [=[
<Form id="1" name="Main" ordinaryFormVersion="2.1"><Caption>Empty</Caption></Form>
]=])
file(WRITE "${test_root}/empty/Form/Module.bsl" "")
run_cli(0 "empty Module.bsl" build "${empty_module_source}" "${empty_module_bin}")

message(STATUS "CLI integration: PASS")
