cmake_minimum_required(VERSION 3.25)
string(RANDOM LENGTH 12 nonce)
file(MAKE_DIRECTORY "${ROOT}/${nonce}")
file(COPY "${EXAMPLES}/seeded-voxel" "${EXAMPLES}/tile-hollow" DESTINATION "${ROOT}/${nonce}")
execute_process(COMMAND "${PROBE}" "${ROOT}/${nonce}/seeded-voxel" "${ROOT}/${nonce}/tile-hollow"
  RESULT_VARIABLE status)
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "Seeded world probe returned ${status}")
endif()
