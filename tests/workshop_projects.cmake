cmake_minimum_required(VERSION 3.25)
string(RANDOM LENGTH 12 nonce)
file(MAKE_DIRECTORY "${ROOT}/${nonce}")
file(COPY "${EXAMPLES}/ore-furnace" "${EXAMPLES}/wild-camp" "${EXAMPLES}/beacon-table"
  DESTINATION "${ROOT}/${nonce}")
execute_process(COMMAND "${PROBE}" "${ROOT}/${nonce}/ore-furnace" "${ROOT}/${nonce}/wild-camp"
  "${ROOT}/${nonce}/beacon-table" RESULT_VARIABLE status)
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "Workshop project probe returned ${status}")
endif()
