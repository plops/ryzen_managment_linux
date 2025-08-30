set(SPDLOG_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/../ryzen_pm_table_moonitor/extern/spdlog)
find_package(spdlog)
if (NOT spdlog_FOUND)
    message(STATUS "spdlog not found in dependencies folder")
    if (EXISTS ${SPDLOG_SUBMODULE_DIR}/CMakeLists.txt)
        add_subdirectory(${SPDLOG_SUBMODULE_DIR} ../ryzen_pm_table_moonitor/extern/spdlog)
        message(STATUS "spdlog will be built from submodule")
    else ()
        include(FetchContent)
        FetchContent_Declare(
                spdlog
                GIT_REPOSITORY https://github.com/gabime/spdlog.git
                GIT_TAG v1.13.0
        )
        FetchContent_MakeAvailable(spdlog)
    endif ()
endif ()