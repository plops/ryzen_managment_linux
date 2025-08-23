#find_package(implot QUIET)
#if (NOT implot_FOUND)
#    # ImPlot
#    FetchContent_Declare(
#            implot
#            GIT_REPOSITORY https://github.com/epezent/implot.git
#            #        GIT_TAG        v0.16
#    )
#    FetchContent_MakeAvailable(implot)
#endif ()

set(implot_SOURCE_DIR /home/kiel/src/implot)
add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)