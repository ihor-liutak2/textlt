# CMake module lexer sample
function(enable_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()
