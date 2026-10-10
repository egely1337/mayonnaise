# Needs FFmpeg's development libraries (libavformat/libavcodec/libswscale/libswresample).
# Ubuntu: libavformat-dev libavcodec-dev libswscale-dev libswresample-dev · Fedora: ffmpeg-free-devel
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MAYO_FFMPEG IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
endif()
if(NOT MAYO_FFMPEG_FOUND)
  message(STATUS "Mayonnaise: FFmpeg not found, skipping the video_player example")
  return()
endif()
mayonnaise_example(video_player main.cpp media_player.cpp)
target_link_libraries(video_player PRIVATE PkgConfig::MAYO_FFMPEG)
