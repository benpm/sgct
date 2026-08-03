##########################################################################################
# SGCT - ImGui Integration                                                               #
##########################################################################################

set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(IMGUI_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends")

add_library(imgui STATIC
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_BACKEND_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_BACKEND_DIR}/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_SOURCE_DIR}
    ${IMGUI_BACKEND_DIR}
)

# Link to GLFW for the backend
target_link_libraries(imgui PUBLIC glfw)

# Disable warnings for third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imgui PRIVATE -w)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(imgui PRIVATE /w)
endif()

# Set C++ standard
target_compile_features(imgui PUBLIC cxx_std_17)

set_target_properties(imgui PROPERTIES FOLDER "External")
