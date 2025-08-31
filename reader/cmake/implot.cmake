message(STATUS "ImPlot will be built from submodule files in extern/implot")
set(implot_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../ryzen_pm_table_moonitor/extern/implot)
add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)
