add_library(mayonnaise
  core/animation.cpp
  core/app.cpp
  core/color.cpp
  core/event_dispatcher.cpp
  core/layout.cpp
  core/test_scene.cpp
  core/widget.cpp
  core/window.cpp
  core/window_root.cpp
  platform/sdl_platform.cpp
  render/gl.cpp
  render/font.cpp
  render/image_io.cpp
  render/painter.cpp
  render/renderer2d.cpp
  render/text_system.cpp
  theme/theme.cpp
)
add_library(mayonnaise::mayonnaise ALIAS mayonnaise)

target_compile_features(mayonnaise PUBLIC cxx_std_20)
set_target_properties(mayonnaise PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(mayonnaise
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# SDL/GL/FreeType are implementation details: public headers never include them.
target_link_libraries(mayonnaise
  PRIVATE
    $<BUILD_INTERFACE:mayonnaise_glad>
    $<BUILD_INTERFACE:mayonnaise_stb>
    SDL3::SDL3
    Freetype::Freetype
    OpenGL::GL)
mayonnaise_set_warnings(mayonnaise)
mayonnaise_set_sanitizers(mayonnaise)

mayonnaise_embed(mayonnaise
  ${PROJECT_SOURCE_DIR}/shaders/ui.vert
  ${PROJECT_SOURCE_DIR}/shaders/ui.frag
  ${PROJECT_SOURCE_DIR}/shaders/fullscreen.vert
  ${PROJECT_SOURCE_DIR}/shaders/blur_down.frag
  ${PROJECT_SOURCE_DIR}/shaders/blur_up.frag
  ${PROJECT_SOURCE_DIR}/assets/fonts/InterVariable.ttf
)
