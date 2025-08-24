set(TASKFLOW_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/extern/taskflow)
find_package(Taskflow)
if (NOT Taskflow_FOUND)
    message(STATUS "taskflow not found in dependencies folder")
    set(TF_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable TaskFlow examples")
    set(TF_BUILD_TESTS OFF CACHE INTERNAL "Disable TaskFlow tests")
    if(EXISTS ${TASKFLOW_SUBMODULE_DIR}/CMakeLists.txt)
        add_subdirectory(${TASKFLOW_SUBMODULE_DIR} extern/taskflow)
    else()
        FetchContent_Declare(
                taskflow
                GIT_REPOSITORY https://github.com/taskflow/taskflow.git
                GIT_TAG v3.10.0
        )
        FetchContent_MakeAvailable(taskflow)
    endif()
endif ()