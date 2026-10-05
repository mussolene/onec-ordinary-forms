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

# Недоверенный пакет не должен читать модуль вне своего каталога.
set(module_escape_root "${test_root}/module-escape/Form")
file(MAKE_DIRECTORY "${module_escape_root}")
file(COPY "${source_xml}" DESTINATION "${test_root}/module-escape")
file(WRITE "${test_root}/outside-module.bsl" "OWN_OUTSIDE_MODULE_MARKER")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${module_escape_root}/Module.bsl" SYMBOLIC)
file(WRITE "${test_root}/module-escape.bin" "preserve output")
run_cli(1 "module input symlink escape" build "${test_root}/module-escape/Form.xml" "${test_root}/module-escape.bin" --json)
require_contains("${CLI_STDOUT}" "OOF0004" "module input boundary diagnostic")
require_file_equals("${test_root}/module-escape.bin" "preserve output" "module escape must not overwrite output")
file(REMOVE "${module_escape_root}/Module.bsl")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${module_escape_root}/Module.bsl")
run_cli(1 "module input hard link escape" build "${test_root}/module-escape/Form.xml" "${test_root}/module-escape.bin" --json)
require_contains("${CLI_STDOUT}" "OOF0004" "module hard link diagnostic")
require_file_equals("${test_root}/module-escape.bin" "preserve output" "module hard link must not overwrite output")
file(REMOVE "${module_escape_root}/Module.bsl")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${module_escape_root}/Module.bsl" SYMBOLIC)

# Source package -> Form.bin -> public XML and module sidecar.
run_cli(0 "initial build" build "${source_xml}" "${first_bin}" --json)
require_contains("${CLI_STDOUT}" "\"ok\":true" "initial build JSON result")

# Все производные пути проверяются до изменения XML или модуля.
file(WRITE "${test_root}/module-escape/Form.xml" "preserve XML")
run_cli(1 "module output symlink escape" dump "${first_bin}" "${test_root}/module-escape/Form.xml" --json)
require_file_equals("${test_root}/outside-module.bsl" "OWN_OUTSIDE_MODULE_MARKER" "dump must preserve outside module")
require_file_equals("${test_root}/module-escape/Form.xml" "preserve XML" "dump rejection must preserve XML")
file(REMOVE "${module_escape_root}/Module.bsl")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${module_escape_root}/Module.bsl")
run_cli(1 "module output hard link escape" dump "${first_bin}" "${test_root}/module-escape/Form.xml" --json)
require_file_equals("${test_root}/outside-module.bsl" "OWN_OUTSIDE_MODULE_MARKER" "dump must preserve hard linked module")
require_file_equals("${test_root}/module-escape/Form.xml" "preserve XML" "hard link rejection must preserve XML")
file(REMOVE "${module_escape_root}/Module.bsl")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${module_escape_root}/Module.bsl" SYMBOLIC)
file(MAKE_DIRECTORY "${test_root}/package-escape")
file(CREATE_LINK "${test_root}/module-escape/Form" "${test_root}/package-escape/Form" SYMBOLIC)
run_cli(1 "sidecar directory symlink escape" dump "${first_bin}" "${test_root}/package-escape/Form.xml" --json)
if(EXISTS "${test_root}/package-escape/Form.xml")
  message(FATAL_ERROR "dump wrote XML before rejecting the package directory symlink")
endif()
file(CREATE_LINK "${test_root}/outside-module.bsl" "${test_root}/linked-output.bin" SYMBOLIC)
run_cli(1 "binary output symlink" build "${source_xml}" "${test_root}/linked-output.bin" --json)
require_file_equals("${test_root}/outside-module.bsl" "OWN_OUTSIDE_MODULE_MARKER" "build must preserve output symlink target")
file(CREATE_LINK "${test_root}/outside-module.bsl" "${test_root}/linked-output.xml" SYMBOLIC)
run_cli(1 "XML output symlink" dump "${first_bin}" "${test_root}/linked-output.xml" --json)
require_file_equals("${test_root}/outside-module.bsl" "OWN_OUTSIDE_MODULE_MARKER" "dump must preserve XML symlink target")

run_cli(0 "initial dump" dump "${first_bin}" "${first_xml}" --json)
require_contains("${CLI_STDOUT}" "\"ok\":true" "initial dump JSON result")
require_file_equals("${test_root}/first/Form/Module.bsl" "${module_text}" "initial module preservation")

# Сигнатура UTF-8 файла модуля не является символом текста BSL.
string(ASCII 239 187 191 module_bom)
file(WRITE "${module_file}" "${module_bom}${module_text}")
set(bom_bin "${test_root}/bom.bin")
set(bom_xml "${test_root}/bom/Form.xml")
run_cli(0 "UTF-8 BOM module build" build "${source_xml}" "${bom_bin}" --json)
file(READ "${first_bin}" plain_binary HEX)
file(READ "${bom_bin}" bom_binary HEX)
if(NOT plain_binary STREQUAL bom_binary)
  message(FATAL_ERROR "UTF-8 module signature changed the serialized BSL text")
endif()
run_cli(0 "UTF-8 BOM module dump" dump "${bom_bin}" "${bom_xml}" --json)
require_file_equals("${test_root}/bom/Form/Module.bsl" "${module_text}" "module signature must not become BSL text")
file(WRITE "${module_file}" "${module_text}")

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

# Неполнота источника предупреждает, но не запрещает сборку известных свойств.
set(partial_source "${test_root}/partial/Form.xml")
set(partial_bin "${test_root}/partial.bin")
file(MAKE_DIRECTORY "${test_root}/partial/Form")
string(REPLACE "ordinaryFormVersion=\"2.1\"" "ordinaryFormVersion=\"2.1\" reconstructionComplete=\"false\""
  partial_xml "${edited_xml}")
file(WRITE "${partial_source}" "${partial_xml}")
file(WRITE "${test_root}/partial/Form/Module.bsl" "${module_text}")
run_cli(0 "partial source build" build "${partial_source}" "${partial_bin}" --json)
string(JSON partial_ok GET "${CLI_STDOUT}" ok)
string(JSON warning_count LENGTH "${CLI_STDOUT}" diagnostics)
string(JSON warning_severity GET "${CLI_STDOUT}" diagnostics 0 severity)
if(NOT partial_ok OR warning_count LESS 1 OR NOT warning_severity STREQUAL "warning")
  message(FATAL_ERROR "partial source must build successfully with JSON warnings")
endif()
file(READ "${partial_bin}" partial_binary HEX)
file(READ "${second_bin}" known_binary HEX)
if(NOT partial_binary STREQUAL known_binary)
  message(FATAL_ERROR "partial metadata must preserve the serialized known properties and module")
endif()

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

# Named Button.Picture resolves to bytes under the Form package and dumps them back out.
set(picture_source "${test_root}/picture/Form.xml")
set(picture_root "${test_root}/picture/Form")
set(picture_path "${picture_root}/Items/Logo/Picture.bmp")
file(MAKE_DIRECTORY "${picture_root}/Items/Logo")
file(WRITE "${picture_root}/Module.bsl" "")
file(WRITE "${picture_path}" "BM-test-picture-bytes")
file(WRITE "${picture_source}" [=[
<Form id="1" name="PictureForm" ordinaryFormVersion="2.1">
  <PictureAssets><PictureAsset id="42" relativePath="Items/Logo/Picture.bmp" format="bmp" transparent="true"/></PictureAssets>
  <ChildItems><Button id="2" name="Logo"><Position/><Picture>42</Picture></Button></ChildItems>
</Form>
]=])
set(picture_bin "${test_root}/picture.bin")
set(picture_dump "${test_root}/picture-dump/Form.xml")
run_cli(0 "Button.Picture build" build "${picture_source}" "${picture_bin}" --json)
run_cli(0 "Button.Picture dump" dump "${picture_bin}" "${picture_dump}" --json)
require_file_equals("${test_root}/picture-dump/Form/Items/Logo/Picture.bmp" "BM-test-picture-bytes"
  "picture bytes must be written to the external package")
file(READ "${picture_dump}" picture_xml)
require_contains("${picture_xml}" "relativePath=\"Items/Logo/Picture.bmp\"" "dumped asset path")
require_contains("${picture_xml}" "format=\"bmp\"" "dumped asset format")
require_contains("${picture_xml}" "transparent=\"true\"" "dumped asset transparency")
set(picture_rebuild "${test_root}/picture-rebuilt.bin")
run_cli(0 "Button.Picture rebuilt dump" build "${picture_dump}" "${picture_rebuild}" --json)
run_cli(0 "Button.Picture rebuilt load" dump "${picture_rebuild}" "${test_root}/picture-rebuilt/Form.xml" --json)
require_file_equals("${test_root}/picture-rebuilt/Form/Items/Logo/Picture.bmp" "BM-test-picture-bytes"
  "picture bytes must survive dump and rebuild")

set(outside_picture_dir "${test_root}/outside-picture-dir")
file(MAKE_DIRECTORY "${outside_picture_dir}")
set(directory_escape_root "${test_root}/directory-escape/Form")
file(MAKE_DIRECTORY "${directory_escape_root}")
file(CREATE_LINK "${outside_picture_dir}" "${directory_escape_root}/Items" SYMBOLIC)
run_cli(1 "picture dump directory symlink escape" dump "${picture_bin}" "${test_root}/directory-escape/Form.xml" --json)
require_contains("${CLI_STDOUT}" "OOF0004" "picture dump directory symlink diagnostic")
if(EXISTS "${outside_picture_dir}/Logo")
  message(FATAL_ERROR "picture dump created an outside directory before rejecting symlink escape")
endif()

set(outside_picture_file "${test_root}/outside-picture.bmp")
file(WRITE "${outside_picture_file}" "BM-preserve")
set(file_escape_root "${test_root}/file-escape/Form")
file(MAKE_DIRECTORY "${file_escape_root}/Items/Logo")
file(CREATE_LINK "${outside_picture_file}" "${file_escape_root}/Items/Logo/Picture.bmp" SYMBOLIC)
run_cli(1 "picture dump file symlink escape" dump "${picture_bin}" "${test_root}/file-escape/Form.xml" --json)
require_contains("${CLI_STDOUT}" "OOF0004" "picture dump file symlink diagnostic")
require_file_equals("${outside_picture_file}" "BM-preserve" "picture dump must not follow an output file symlink")

file(REMOVE "${picture_path}")
run_cli(1 "missing picture file" build "${picture_source}" "${test_root}/picture-missing.bin" --json)
require_contains("${CLI_STDOUT}" "OOF0006" "missing picture file diagnostic")
file(WRITE "${test_root}/outside.bmp" "BM-outside")
file(CREATE_LINK "${test_root}/outside.bmp" "${picture_path}" SYMBOLIC)
run_cli(1 "picture symlink escape" build "${picture_source}" "${test_root}/picture-escape.bin" --json)
require_contains("${CLI_STDOUT}" "OOF0006" "picture symlink escape diagnostic")

# Отказ по размеру происходит до чтения содержимого и изменения результата.
set(oversized_input "${test_root}/oversized-input")
string(REPEAT "x" 1048576 input_chunk)
file(WRITE "${oversized_input}" "")
foreach(chunk RANGE 1 64)
  file(APPEND "${oversized_input}" "${input_chunk}")
endforeach()
unset(input_chunk)
file(APPEND "${oversized_input}" "x")
foreach(command dump build)
  run_cli(1 "oversized ${command} input" "${command}" "${oversized_input}" "${protected_output}" --json)
  require_contains("${CLI_STDOUT}" "64 MiB" "input size limit diagnostic")
  require_file_equals("${protected_output}" "preserve this output" "oversized input must preserve output")
endforeach()
file(RENAME "${oversized_input}" "${test_root}/empty/Form/Module.bsl")
run_cli(1 "oversized module input" build "${empty_module_source}" "${protected_output}" --json)
require_contains("${CLI_STDOUT}" "64 MiB" "module size limit diagnostic")
require_file_equals("${protected_output}" "preserve this output" "oversized module must preserve output")
file(REMOVE "${test_root}/empty/Form/Module.bsl")

# Два допустимых по отдельности файла также ограничены общим бюджетом.
file(REMOVE "${picture_path}")
set(second_picture "${picture_root}/Items/Second/Picture.bmp")
file(MAKE_DIRECTORY "${picture_root}/Items/Second")
file(WRITE "${picture_path}" "BM")
file(WRITE "${second_picture}" "BM")
string(REPEAT "x" 1048576 input_chunk)
foreach(chunk RANGE 1 32)
  file(APPEND "${picture_path}" "${input_chunk}")
  file(APPEND "${second_picture}" "${input_chunk}")
endforeach()
unset(input_chunk)
file(WRITE "${picture_source}" [=[
<Form id="1" name="PictureBudget" ordinaryFormVersion="2.1">
  <PictureAssets>
    <PictureAsset id="42" relativePath="Items/Logo/Picture.bmp" format="bmp"/>
    <PictureAsset id="43" relativePath="Items/Second/Picture.bmp" format="bmp"/>
  </PictureAssets>
  <ChildItems>
    <Button id="2" name="Logo"><Position/><Picture>42</Picture></Button>
    <Button id="3" name="Second"><Position/><Picture>43</Picture></Button>
  </ChildItems>
</Form>
]=])
run_cli(1 "aggregate picture limit" build "${picture_source}" "${protected_output}" --json)
require_contains("${CLI_STDOUT}" "OOF0006" "picture budget diagnostic")
require_contains("${CLI_STDOUT}" "remaining picture package limit" "aggregate size diagnostic")
require_file_equals("${protected_output}" "preserve this output" "picture budget must preserve output")
file(REMOVE "${picture_path}" "${second_picture}")

message(STATUS "CLI integration: PASS")
