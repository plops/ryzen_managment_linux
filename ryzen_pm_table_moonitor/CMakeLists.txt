cmake_minimum_required(VERSION 3.22)
project(PM_Table_Monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(extern)

# change cxxflags to use graphite loop otimizations only for release and relwithdebinfo
# -march=native -O3 -floop-parallelize-all -floop-nest-optimize -floop-interchange
# and enable -flto for release

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release build")
    set(CMAKE_CXX_FLAGS_RELEASE
            "-O3 -march=native -floop-parallelize-all -floop-nest-optimize -floop-interchange"
    )
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "RelWithDebInfo build")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
            "-O2 -g -march=native --floop-parallelize-all -floop-nest-optimize -floop-interchange"
    )
else()
    message(STATUS "Debug build")
    set(CMAKE_CXX_FLAGS_DEBUG
            "-O0 -g"
    )
endif()

# add options to eneable asan, ubsan and lsan (together) or tsan only
option(ENABLE_ASAN "Enable AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if (ENABLE_ASAN)
    message(STATUS "Enabling AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined,leak -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address,undefined,leak")
endif()
if (ENABLE_TSAN)
    message(STATUS "Enabling ThreadSanitizer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Try to find spdlog, otherwise fetch it
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found, using FetchContent to download it")
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

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
