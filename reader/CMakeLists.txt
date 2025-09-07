# CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(pm_table_reader CXX)

# Specify C++20 as the required standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include custom modules
include(flags)
include(sanitizers)
include(PGO)
include(spdlog)
include(folly)
include(imgui)
include(glfw)
include(implot)

# Add the executable target from our source file
add_executable(pm_reader main.cpp)

# Ensure the pthreads library is linked for std::thread support
find_package(Threads REQUIRED)
target_link_libraries(pm_reader PRIVATE Threads::Threads)

# Build pm_measure with split sources
add_executable(pm_measure
        measure.cpp
        pm_table_reader.cpp
        realtime_guard.cpp
        locked_buffer.cpp
        gui_runner.cpp
        gui_render.cpp
)

target_link_libraries(pm_measure
        PRIVATE
        Threads::Threads
        spdlog::spdlog
        imgui
        implot
        glfw
        GL
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/results)


# Optional: Create an "install" target
install(TARGETS pm_reader pm_measure DESTINATION bin)