find_package(taskflow QUIET)
if (NOT taskflow_FOUND)
    # TaskFlow
    # --- FIX: Disable building of examples and tests to prevent target name collisions ---
    set(TF_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable TaskFlow examples")
    set(TF_BUILD_TESTS OFF CACHE INTERNAL "Disable TaskFlow tests")
    FetchContent_Declare(
            taskflow
            GIT_REPOSITORY https://github.com/taskflow/taskflow.git
            GIT_TAG v3.10.0 # Using a specific tag is better practice
    )
    FetchContent_MakeAvailable(taskflow)
endif ()