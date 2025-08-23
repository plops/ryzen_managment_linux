include(FetchContent)



# GLFW
# --- FIX: Disable building of examples ---


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




# Glad include (generated in source dir)
#target_include_directories(glad PUBLIC ${glad_SOURCE_DIR}/include)