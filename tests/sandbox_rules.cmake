set(path "${ROOT}/rules.txt")
file(MAKE_DIRECTORY "${ROOT}")
file(REMOVE "${path}")

function(run expected pattern)
  execute_process(
    COMMAND "${CLI}" --sandbox-rules "${path}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE detail
  )
  if(NOT status STREQUAL expected OR NOT "${output}${detail}" MATCHES "${pattern}")
    message(FATAL_ERROR "Unexpected result: ${status} ${output}${detail}")
  endif()
endfunction()

run(0 "jump_speed=6" show)
run(0 "preview: jump_speed 6 -> 8" preview 6 8)
if(EXISTS "${path}")
  message(FATAL_ERROR "Preview changed the rules file")
endif()
run(1 "invalid_argument" apply 6 99)
run(0 "applied: jump_speed 6 -> 8" apply 6 8)
run(1 "rule_conflict: jump_speed=8" apply 6 7)
run(0 "jump_speed=8" show)
run(0 "applied: jump_speed 8 -> 6" apply 8 6)
run(0 "jump_speed=6" show)
file(WRITE "${path}" "invalid\n")
run(1 "invalid_rules" apply 6 8)
file(READ "${path}" after)
if(NOT after STREQUAL "invalid\n")
  message(FATAL_ERROR "Invalid rules must remain unchanged")
endif()

function(project_run directory expected pattern)
  execute_process(
    COMMAND "${CLI}" --sandbox-project "${directory}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE detail
  )
  if(NOT status STREQUAL expected OR NOT "${output}${detail}" MATCHES "${pattern}")
    message(FATAL_ERROR "Unexpected project result: ${status} ${output}${detail}")
  endif()
endfunction()

string(RANDOM LENGTH 12 nonce)
set(first "${ROOT}/试验 ${nonce}")
set(second "${ROOT}/second-${nonce}")
project_run("${first}" 1 "invalid_rules" show)
project_run("${first}" 0 "created: voxel-sandbox-v1" create)
project_run("${second}" 0 "created: voxel-sandbox-v1 preset=mesa" create mesa)
project_run("${first}" 0 "applied: jump_speed 6 -> 8" apply 6 8)
project_run("${first}" 1 "create_failed" create)
project_run("${first}" 0 "jump_speed=8" show)
project_run("${first}" 0 "placement=stone.*stone=breakable" show)
project_run("${second}" 0 "placement=dirt.*stone=locked.*jump_speed=8" show)
set(content "${second}/sandbox-content-v1.txt")
file(WRITE "${content}"
  "wds-sandbox-content-v1\nsky 101010\nplace grass\ngrass 111111 breakable\ndirt 222222 breakable\nstone 333333 breakable\n")
project_run("${second}" 0 "placement=grass.*sky=101010.*stone=breakable" show)
file(APPEND "${content}" "sky 000000\n")
project_run("${second}" 1 "invalid_content" show)
foreach(directory IN ITEMS "${first}" "${second}")
  file(REMOVE "${directory}/sandbox-rules-v1.txt" "${directory}/sandbox-content-v1.txt")
  # Leave the empty test directory in build; no recursive removal is needed.
endforeach()
