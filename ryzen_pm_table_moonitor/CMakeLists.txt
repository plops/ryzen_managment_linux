cmake_minimum_required(VERSION 3.22)
project(PM_Table_Monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

include(cmake/flags.cmake)
include(cmake/sanitizers.cmake)
include(cmake/spdlog.cmake)
include(cmake/boost.cmake)
include(cmake/glfw.cmake)
include(cmake/imgui.cmake)
include(cmake/implot.cmake)
include(cmake/taskflow.cmake)

add_executable(pm_monitor
        src/main.cpp
        src/pm_table_reader.cpp
        src/analysis_manager.cpp
)

target_link_libraries(pm_monitor PRIVATE
        Taskflow::Taskflow
        imgui
        implot
        glfw
        spdlog::spdlog
)

if (WIN32)
    target_link_libraries(pm_monitor PRIVATE opengl32)
else ()
    target_link_libraries(pm_monitor PRIVATE GL)
endif ()