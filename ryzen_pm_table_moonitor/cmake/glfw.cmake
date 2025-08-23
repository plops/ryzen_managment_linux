set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable GLFW examples")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Disable GLFW documentation")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Disable GLFW tests")
find_package(glfw3)
if (NOT glfw3_FOUND)
    message(STATUS "glfw not found")
    FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(glfw)
endif ()