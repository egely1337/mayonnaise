# Linux-only: reads /proc and /sys.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  mayonnaise_example(task_manager main.cpp system_stats.cpp widgets.cpp)
endif()
