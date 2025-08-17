# CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(pm_table_reader CXX)

# Specify C++20 as the required standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add the executable target from our source file
add_executable(pm_reader main.cpp)

# Enable common compiler warnings for better code quality
target_compile_options(pm_reader PRIVATE -Wall -Wextra -pedantic)

# Ensure the pthreads library is linked for std::thread support
find_package(Threads REQUIRED)
target_link_libraries(pm_reader PRIVATE Threads::Threads)

# Optional: Create an "install" target
install(TARGETS pm_reader DESTINATION bin)