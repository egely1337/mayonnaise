# Linux-only: reads /proc and /sys.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  mayonnaise_app(taskmanager ID io.github.egely1337.Mayonnaise.TaskManager
    SOURCES main.cpp system_stats.cpp widgets.cpp)
endif()
