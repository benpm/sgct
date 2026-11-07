##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2026                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(simplecube main.cpp box.cpp box.h bloom_effect.cpp bloom_effect.h)
set_compile_options(simplecube)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(stbimage CONFIG REQUIRED)
target_link_libraries(simplecube
  PRIVATE sgct::sgct glm::glm imgui::imgui unofficial::stbimage::stbimage-read-impl
)

set_property(TARGET simplecube PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:simplecube>)
