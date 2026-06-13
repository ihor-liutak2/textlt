if(NOT DEFINED PATCH_FILE)
  message(FATAL_ERROR "PATCH_FILE is required")
endif()

execute_process(
  COMMAND git apply --check --ignore-whitespace "${PATCH_FILE}"
  RESULT_VARIABLE patch_check_result
)

if(patch_check_result EQUAL 0)
  execute_process(
    COMMAND git apply --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE patch_result
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply patch: ${PATCH_FILE}")
  endif()
  return()
endif()

execute_process(
  COMMAND git apply --reverse --check --ignore-whitespace "${PATCH_FILE}"
  RESULT_VARIABLE reverse_check_result
)

if(reverse_check_result EQUAL 0)
  message(STATUS "Patch already applied: ${PATCH_FILE}")
  return()
endif()

message(FATAL_ERROR "Patch cannot be applied cleanly: ${PATCH_FILE}")
