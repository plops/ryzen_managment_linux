cmake_minimum_required(VERSION 3.22)
project(PM_Table_Monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} /home/kiel/deps/lib64/cmake)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} /home/kiel/deps/lib64/cmake)

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
)

target_link_libraries(pm_monitor PRIVATE
        Taskflow::Taskflow
        imgui
        implot
        glfw
        spdlog::spdlog
        #    glad
)

if (WIN32)
    target_link_libraries(pm_monitor PRIVATE opengl32)
else ()
    target_link_libraries(pm_monitor PRIVATE GL)
endif ()

# Add the ImGui include directory manually (less preferred method)
#target_include_directories(pm_monitor PRIVATE
#        ${CMAKE_BINARY_DIR}/_deps/imgui-src
#        ${CMAKE_BINARY_DIR}/_deps/imgui-src/backends
#        ${CMAKE_BINARY_DIR}/_deps/implot-src
#)