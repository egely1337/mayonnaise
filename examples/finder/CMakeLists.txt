# Linux-only: inotify, the freedesktop.org Trash and xdg-open.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  mayonnaise_example(finder main.cpp browser.cpp file_icons.cpp file_model.cpp file_ops.cpp panels.cpp preview_cache.cpp
                     views.cpp)
endif()
