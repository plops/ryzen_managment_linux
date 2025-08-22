cmake_minimum_required(VERSION 3.22)
project(PM_Table_Monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(extern)

# change cxxflags
set(CMAKE_CXX_FLAGS
        "-march=native -O3"
)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

add_executable(pm_monitor
        src/main.cpp
        src/pm_table_reader.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui_widgets.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui_tables.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui_draw.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui.h
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/imgui_internal.h
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/backends/imgui_impl_opengl3.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src/backends/imgui_impl_glfw.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/implot-src/implot.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/implot-src/implot_items.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/implot-src/implot_internal.h
        ${CMAKE_CURRENT_BINARY_DIR}/_deps/implot-src/implot.h
)

target_link_libraries(pm_monitor PRIVATE
        Taskflow::Taskflow
#    imgui
#    implot
    glfw
#    glad
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

