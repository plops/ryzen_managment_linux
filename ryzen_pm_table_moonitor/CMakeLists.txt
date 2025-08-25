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
include(cmake/tomlplusplus.cmake)

add_executable(pm_monitor
        src/main.cpp
        src/pm_table_reader.cpp
        src/analysis_manager.cpp
        src/measurement_namer.cpp
)

target_link_libraries(pm_monitor PRIVATE
        Taskflow::Taskflow
        imgui
        implot
        glfw
        spdlog::spdlog
        tomlplusplus::tomlplusplus
)

if (WIN32)
    target_link_libraries(pm_monitor PRIVATE opengl32)
else ()
    target_link_libraries(pm_monitor PRIVATE GL)
endif ()

# Command to copy the .toml file to the output directory
add_custom_command(TARGET pm_monitor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pm_table_names.toml
        $<TARGET_FILE_DIR:pm_monitor>
        COMMENT "Copying pm_table_names.toml to output directory"
)