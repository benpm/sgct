##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2025                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(simplecube main.cpp box.cpp box.h bloom_effect.cpp bloom_effect.h)
set_compile_options(simplecube)
target_link_libraries(simplecube PRIVATE sgct::sgct imgui)

if (WIN32)
  add_custom_command(TARGET simplecube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:simplecube> $<TARGET_FILE_DIR:simplecube>
    COMMAND_EXPAND_LISTS
  )
endif ()
