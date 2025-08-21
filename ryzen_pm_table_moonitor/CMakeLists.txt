cmake_minimum_required(VERSION 3.22)
project(PM_Table_Monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(extern)

add_executable(pm_monitor src/main.cpp src/pm_table_reader.cpp)

target_link_libraries(pm_monitor PRIVATE
    taskflow
    imgui
    implot
    glfw
    glad
)
# Add the ImGui include directory manually (less preferred method)
target_include_directories(pm_monitor PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/imgui-src
        ${CMAKE_BINARY_DIR}/_deps/imgui-src/backends
        ${CMAKE_BINARY_DIR}/_deps/implot-src
)
if(WIN32)
    target_link_libraries(pm_monitor PRIVATE opengl32)
else()
    target_link_libraries(pm_monitor PRIVATE GL)
endif()

