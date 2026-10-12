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

# mayonnaise_embed_spirv(<target> <shader>...)
# Compiles GLSL shaders (stage from the extension) to SPIR-V for the Vulkan backend and embeds the result as
# `<name>_spv` (ui.frag -> ui_frag_spv). The sources are the same files the OpenGL backend compiles at run
# time; they switch on the VULKAN macro the SPIR-V compilers predefine.
function(mayonnaise_embed_spirv target)
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/spirv)
  file(MAKE_DIRECTORY ${out_dir})
  set(outputs)
  foreach(file IN LISTS ARGN)
    get_filename_component(name ${file} NAME)
    get_filename_component(ext ${file} LAST_EXT)
    string(SUBSTRING ${ext} 1 -1 stage)
    set(out ${out_dir}/${name}.spv)
    if(MAYONNAISE_GLSLC)
      set(cmd ${MAYONNAISE_GLSLC} --target-env=vulkan1.3 -O -fshader-stage=${stage} ${file} -o ${out})
    else()
      set(cmd ${MAYONNAISE_GLSLANG} -V --target-env vulkan1.3 -S ${stage} ${file} -o ${out})
    endif()
    add_custom_command(OUTPUT ${out} COMMAND ${cmd} DEPENDS ${file} COMMENT "Compiling ${name} to SPIR-V" VERBATIM)
    list(APPEND outputs ${out})
  endforeach()
  mayonnaise_embed(${target} ${outputs})
endfunction()
