##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2025                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(simplecube main.cpp box.cpp box.h)
set_compile_options(simplecube)
target_link_libraries(simplecube PRIVATE sgct::sgct)
