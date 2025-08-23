include(FetchContent)

# TaskFlow
# --- FIX: Disable building of examples and tests to prevent target name collisions ---
#set(TF_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable TaskFlow examples")
#set(TF_BUILD_TESTS OFF CACHE INTERNAL "Disable TaskFlow tests")
FetchContent_Declare(
        taskflow
        GIT_REPOSITORY https://github.com/taskflow/taskflow.git
        GIT_TAG        v3.10.0 # Using a specific tag is better practice
)
FetchContent_MakeAvailable(taskflow)

# GLFW
# --- FIX: Disable building of examples ---
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Disable GLFW examples")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Disable GLFW documentation")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Disable GLFW tests")
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# GLAD
#set(GLAD_GENERATOR "c")
#set(GLAD_PROFILE "core")
#set(GLAD_API "gl=3.3") # Or whatever version you need
#set(GLAD_EXTENSIONS "") # Add extensions here if needed
#set(GLAD_SPEC "gl")
#FetchContent_Declare(
#        glad
#        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
#        GIT_TAG        v2.0.8 # 0.1.36
#)
#FetchContent_MakeAvailable(glad)

# ImGui
# ImGui doesn't automatically add examples as targets, so it's usually safe.
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.2b
)

# implot v0.16 has issues with imgui v1.92.1: font->FindGlyph
#‘struct ImFont’ has no member named ‘FontSize’
#‘struct ImFont’ has no member named ‘FindGlyph’
# they moved into baked font substructure

FetchContent_MakeAvailable(imgui)

# ImPlot
FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
#        GIT_TAG        v0.16
)

FetchContent_MakeAvailable(implot)


# ImGui target
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw) # glad
#target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# ImPlot target
add_library(implot STATIC
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

# Glad include (generated in source dir)
#target_include_directories(glad PUBLIC ${glad_SOURCE_DIR}/include)