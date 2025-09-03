# cmake/PGO.cmake
# -----------------------------------------------------------------------------
# Enables Profile Guided Optimization (PGO) for GCC and Clang.
#
# This module defines two custom build types:
#
# 1. PGO_Instrument:
#    Builds the code with instrumentation to generate a profile.
#
# 2. PGO_Optimize:
#    Uses the generated profiling data to build an optimized executable.
#
# The PGO builds reuse the same optimization flags as the Release build
# (high optimization, march/mtune native, loop/graphite hints). The
# instrumented build adds -fprofile-generate and the optimized build
# adds -fprofile-use -fprofile-correction. For PGO_Optimize we also enable
# interprocedural optimization (LTO) and define NDEBUG to match Release.
#
# Recommended workflow (Ninja):
#   ./scripts/pgo_ninja.sh [build-dir] [jobs] [-- <pm_measure args>]
#
# -----------------------------------------------------------------------------

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")

    # Keep these in sync with the Release options in flags.cmake (mirrors the intent).
    set(_pgo_common_opts
        "-O3"
        "-march=native"
        "-mtune=native"
        "-ffast-math"
        "-floop-parallelize-all"
        "-floop-nest-optimize"
        "-floop-interchange"
        "-faggressive-loop-optimizations"
        "-fexpensive-optimizations"
        "-fgraphite"
        "-fgraphite-identity"
    )

    # Build type for generating PGO profiling data (instrumented) -- same optimizations as Release + profile-generate
    string(REPLACE ";" " " _pgo_instr_flags "${_pgo_common_opts}")
    set(CMAKE_CXX_FLAGS_PGO_Instrument
        "${_pgo_instr_flags} -fprofile-generate" CACHE STRING "Flags for PGO instrumentation build")
    set(CMAKE_EXE_LINKER_FLAGS_PGO_Instrument
        "-fprofile-generate" CACHE STRING "Linker flags for PGO instrumentation build")

    # Build type for using PGO profiling data to optimize -- same optimizations as Release + profile-use
    string(REPLACE ";" " " _pgo_opt_flags "${_pgo_common_opts}")
    set(CMAKE_CXX_FLAGS_PGO_Optimize
        "${_pgo_opt_flags} -fprofile-use -fprofile-correction" CACHE STRING "Flags for PGO optimized build")
    set(CMAKE_EXE_LINKER_FLAGS_PGO_Optimize
        "-fprofile-use -fprofile-correction" CACHE STRING "Linker flags for PGO optimized build")

    # For the optimized PGO build, enable interprocedural optimization (LTO) and NDEBUG to mirror Release.
    if(CMAKE_BUILD_TYPE STREQUAL "PGO_Optimize")
        message(STATUS "Configuring PGO_Optimize: enabling interprocedural optimization and NDEBUG (matches Release).")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        add_compile_definitions(NDEBUG)
    endif()

    # Mark these variables as advanced so they don't show up in the default ccmake/cmake-gui view
    mark_as_advanced(
        CMAKE_CXX_FLAGS_PGO_Instrument
        CMAKE_EXE_LINKER_FLAGS_PGO_Instrument
        CMAKE_CXX_FLAGS_PGO_Optimize
        CMAKE_EXE_LINKER_FLAGS_PGO_Optimize
    )

    message(STATUS "PGO custom build types (PGO_Instrument, PGO_Optimize) are available and use Release-equivalent optimization flags.")

endif()