# glad: generated with `glad --api gl:core=3.3 --extensions GL_KHR_debug c`
add_library(mayonnaise_glad STATIC glad/src/gl.c)
target_include_directories(mayonnaise_glad SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/glad/include>)
set_target_properties(mayonnaise_glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mayonnaise_stb INTERFACE)
target_include_directories(mayonnaise_stb SYSTEM INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>)
