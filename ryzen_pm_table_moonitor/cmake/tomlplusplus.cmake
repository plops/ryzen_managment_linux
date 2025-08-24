set(TOMLPLUSPLUS_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/extern/tomlplusplus)
find_package(tomlplusplus)
if (NOT tomlplusplus_FOUND)
    message(STATUS "tomlplusplus not found in dependencies folder")

    # Set the option to build C++ modules BEFORE adding the subdirectory or making content available.
    set(TOMLPLUSPLUS_BUILD_MODULES ON CACHE BOOL "Enable tomlplusplus C++ modules")

    if(EXISTS ${TOMLPLUSPLUS_SUBMODULE_DIR}/CMakeLists.txt)
        message(STATUS "tomlplusplus will be built from submodule")
        add_subdirectory(${TOMLPLUSPLUS_SUBMODULE_DIR} extern/tomlplusplus)
    else()
        FetchContent_Declare(
                tomlplusplus
                GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
                GIT_TAG v3.4.0 # Or any other recent, stable version
        )
        FetchContent_MakeAvailable(tomlplusplus)
    endif()
endif ()