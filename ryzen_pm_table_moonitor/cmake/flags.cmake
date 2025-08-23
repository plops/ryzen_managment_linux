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
else ()
    message(STATUS "Debug build")
    set(CMAKE_CXX_FLAGS_DEBUG
            "-O0 -g"
    )
endif ()