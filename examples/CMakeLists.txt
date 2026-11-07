##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2026                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

# The examples need the "examples" feature of vcpkg.json for their extra dependencies:
#   cmake --preset linux-ninja-debug -DSGCT_BUILD_EXAMPLES=ON -DVCPKG_MANIFEST_FEATURES=examples

add_subdirectory(simplecube)
