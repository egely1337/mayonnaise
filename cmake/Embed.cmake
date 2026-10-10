# mayonnaise_embed(<target> <symbol> <file>...)
# Generates a .cpp with each file as a `std::span<const unsigned char>` so shaders and fonts ship
# inside the library and users never deal with asset paths.
set(_MAYO_EMBED_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/EmbedFile.cmake)

function(mayonnaise_embed target)
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/embedded)
  foreach(file IN LISTS ARGN)
    get_filename_component(name ${file} NAME)
    string(MAKE_C_IDENTIFIER ${name} ident)
    set(out ${out_dir}/${ident}.cpp)
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -DINPUT=${file} -DOUTPUT=${out} -DIDENT=${ident} -P ${_MAYO_EMBED_SCRIPT}
      DEPENDS ${file} ${_MAYO_EMBED_SCRIPT}
      COMMENT "Embedding ${name}"
      VERBATIM)
    target_sources(${target} PRIVATE ${out})
  endforeach()
endfunction()
