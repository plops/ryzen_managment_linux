# change cxxflags to use graphite loop otimizations only for release and relwithdebinfo
# -march=native -O3 -floop-parallelize-all -floop-nest-optimize -floop-interchange
# and enable -flto for release

add_compile_options(-Wall -Wextra -pedantic)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release build")
    add_compile_options(
            -O3
            -march=native
            -mtune=native
            -ffast-math
            -floop-parallelize-all
            -floop-nest-optimize
            -floop-interchange
    )
    add_compile_definitions(NDEBUG)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "RelWithDebInfo build")
    add_compile_options(
            -O2
            -g
            -march=native
            -floop-parallelize-all
            -floop-nest-optimize
            -floop-interchange
    )
else ()
    message(STATUS "Debug build")
    add_compile_options(
            -O0
            -g
    )
    add_compile_definitions(
            DEBUG
    )
endif ()