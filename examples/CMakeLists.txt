function(mayonnaise_example name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE mayonnaise::mayonnaise)
  mayonnaise_set_warnings(${name})
  mayonnaise_set_sanitizers(${name})
endfunction()

add_subdirectory(hello)
add_subdirectory(gallery)
