# CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(pm_table_reader CXX)

# Specify C++20 as the required standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/flags.cmake)
include(cmake/sanitizers.cmake)
include(cmake/spdlog.cmake)

# Add the executable target from our source file
add_executable(pm_reader main.cpp)

# Ensure the pthreads library is linked for std::thread support
find_package(Threads REQUIRED)
target_link_libraries(pm_reader PRIVATE Threads::Threads)

# Build pm_measure with split sources
add_executable(pm_measure
    measure.cpp
    pm_table_reader.cpp
    eye_diagram.cpp
    eye_capturer.cpp
)

target_link_libraries(pm_measure
        PRIVATE
        Threads::Threads
        spdlog::spdlog
)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/results)


# Optional: Create an "install" target
install(TARGETS pm_reader pm_measure DESTINATION bin)