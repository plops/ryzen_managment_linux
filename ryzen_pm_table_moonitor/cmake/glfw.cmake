set(GLFW_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/extern/glfw)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable GLFW examples")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Disable GLFW documentation")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Disable GLFW tests")
if(EXISTS ${GLFW_SUBMODULE_DIR}/CMakeLists.txt)
    message(STATUS "glfw will be built from submodule")
    add_subdirectory(${GLFW_SUBMODULE_DIR} extern/glfw)
else()
    find_package(glfw3)
    if (NOT glfw3_FOUND)
        message(STATUS "glfw not found use FetchContent to get it")
        FetchContent_Declare(
                glfw
                GIT_REPOSITORY https://github.com/glfw/glfw.git
                GIT_TAG 3.4
        )
        FetchContent_MakeAvailable(glfw)
    endif ()
endif()
