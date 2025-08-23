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